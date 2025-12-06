/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include "filterx/filterx-eval.h"
#include "filterx/filterx-error.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-config.h"
#include "logpipe.h"
#include "scratch-buffers.h"
#include "tls-support.h"

TLS_BLOCK_START
{
  FilterXEvalContext *eval_context;
}
TLS_BLOCK_END;

#define eval_context __tls_deref(eval_context)

#define FAILURE_INFO_PREALLOC_SIZE 16

FilterXEvalContext *
filterx_eval_get_context(void)
{
  return eval_context;
}

FilterXScope *
filterx_eval_get_scope(void)
{
  if (eval_context)
    return eval_context->scope;
  return NULL;
}

void
filterx_eval_set_context(FilterXEvalContext *context)
{
  eval_context = context;
}

static void
_backfill_error_expr(FilterXEvalContext *context)
{
  if (!context)
    return;

  if (context->error_count < 2)
    return;

  FilterXError *last_error = &context->errors[context->error_count - 1];
  FilterXExpr *last_error_expr = last_error->expr;
  if (!last_error_expr || !last_error_expr->lloc)
    return;

  for (gint i = context->error_count - 2; i >= 0; i--)
    {
      FilterXError *error = &context->errors[i];
      if (error->expr && error->expr->lloc)
        break;

      error->expr = last_error_expr;
    }
}

static FilterXError *
_push_new_error(FilterXEvalContext *context)
{
  if (context->error_count == FILTERX_CONTEXT_ERROR_STACK_SIZE)
    {
      msg_warning_once("FilterX: Reached maximum error stack size. "
                       "Increase this number and recompile or contact the AxoSyslog authors",
                       evt_tag_int("max_error_stack_size", FILTERX_CONTEXT_ERROR_STACK_SIZE));
      return NULL;
    }

  context->error_count++;

  return &context->errors[context->error_count - 1];
}

static FilterXError *
_init_local_or_context_error(FilterXEvalContext *context, FilterXError *local_error)
{
  if (context)
    return _push_new_error(context);

  memset(local_error, 0, sizeof(*local_error));
  return local_error;
}

static void
_log_to_stderr_if_needed(FilterXEvalContext *context, FilterXError *error)
{
  if (!context)
    msg_error("FILTERX ERROR", filterx_error_format_location_tag(error), filterx_error_format_tag(error));
}

static void
_clear_local_error(FilterXEvalContext *context, FilterXError *error)
{
  if (!error)
    return;

  gboolean is_error_local = !context;
  if (is_error_local)
    filterx_error_clear(error);
}

void
filterx_eval_push_error(const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXError local_error;
  FilterXError *error = _init_local_or_context_error(context, &local_error);
  if (!error)
    return;

  filterx_error_set_values(error, message, expr, object);

  _backfill_error_expr(context);
  _log_to_stderr_if_needed(context, error);
  _clear_local_error(context, error);
}

void
filterx_eval_push_falsy_error(const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXError local_error;
  FilterXError *error = _init_local_or_context_error(context, &local_error);
  if (!error)
    return;

  filterx_falsy_error_set_values(error, message, expr, object);

  _backfill_error_expr(context);
  _log_to_stderr_if_needed(context, error);
  _clear_local_error(context, error);
}

void
filterx_eval_push_error_static_info(const gchar *message, FilterXExpr *expr, const gchar *info)
{
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXError local_error;
  FilterXError *error = _init_local_or_context_error(context, &local_error);
  if (!error)
    return;

  filterx_error_set_values(error, message, expr, NULL);
  filterx_error_set_static_info(error, info);

  _backfill_error_expr(context);
  _log_to_stderr_if_needed(context, error);
  _clear_local_error(context, error);
}

void
filterx_eval_push_error_info_printf(const gchar *message, FilterXExpr *expr, const gchar *fmt, ...)
{
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXError local_error;
  FilterXError *error = _init_local_or_context_error(context, &local_error);
  if (!error)
    return;

  filterx_error_set_values(error, message, expr, NULL);

  va_list args;
  va_start(args, fmt);
  filterx_error_set_vinfo(error, fmt, args);
  va_end(args);

  _backfill_error_expr(context);
  _log_to_stderr_if_needed(context, error);
  _clear_local_error(context, error);
}

static void
_clear_errors(FilterXEvalContext *context)
{
  for (gint i = 0; i < context->error_count; i++)
    {
      filterx_error_clear(&context->errors[i]);
    }

  context->error_count = 0;
}

void
filterx_eval_clear_errors(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (!context)
    return;

  _clear_errors(context);
}

const gchar *
filterx_eval_get_last_error(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return filterx_eval_get_error(context->error_count - 1);
}

const gchar *
filterx_eval_get_error(gint index)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  g_assert(context);
  g_assert(context->error_count);
  g_assert(index < context->error_count);

  return filterx_error_format(&context->errors[index]);
}

gint
filterx_eval_get_error_count(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return context ? context->error_count : 0;
}

EVTTAG *
filterx_eval_format_error_tag(gint index)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  g_assert(context);
  g_assert(context->error_count);
  g_assert(index < context->error_count);

  return filterx_error_format_tag(&context->errors[index]);
}

EVTTAG *
filterx_eval_format_error_location_tag(gint index)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  g_assert(context);
  g_assert(context->error_count);
  g_assert(index < context->error_count);

  return filterx_error_format_location_tag(&context->errors[index]);
}

void
filterx_eval_dump_errors(const gchar *message)
{
  if (debug_flag)
    {
      gint error_count = filterx_eval_get_error_count();
      gchar buf[FILTERX_EVAL_ERROR_IDX_FMT_SIZE];

      for (gint err_idx = 0; err_idx < error_count; err_idx++)
        {
          msg_debug(message,
                    filterx_eval_format_error_index_tag(err_idx, buf),
                    filterx_eval_format_error_location_tag(err_idx),
                    filterx_eval_format_error_tag(err_idx));
        }
    }

  filterx_eval_clear_errors();
}

static inline FilterXFailureInfo *
_new_failure_info_entry(FilterXEvalContext *context)
{
  GArray *finfo = context->failure_info;
  g_assert(finfo);

  g_array_set_size(finfo, finfo->len + 1);

  return &g_array_index(finfo, FilterXFailureInfo, finfo->len - 1);
}

static void
_fill_failure_info(FilterXEvalContext *context, FilterXExpr *block, FilterXObject *block_res)
{
  if (!context->failure_info)
    return;

  g_assert(context->error_count);

  FilterXError *error = &context->errors[context->error_count - 1];

  if (error->falsy && !context->failure_info_collect_falsy)
    return;

  FilterXFailureInfo *failure_info = _new_failure_info_entry(context);
  failure_info->meta = filterx_object_ref(context->current_frame_meta);

  if (!error->message && context->failure_info_collect_falsy)
    {
      filterx_falsy_error_set_values(&failure_info->errors[0], "Falsy expression", block, block_res);
      failure_info->error_count = 1;
      return;
    }

  for (gint i = 0; i < context->error_count; i++)
    filterx_error_copy(&context->errors[i], &failure_info->errors[i]);
  failure_info->error_count = context->error_count;
}

FilterXEvalResult
filterx_eval_exec(FilterXEvalContext *context, FilterXExpr *expr)
{
  FilterXEvalResult result = FXE_FAILURE;

  FilterXObject *res = filterx_expr_eval(expr);
  if (!res)
    {
      /* Open coded as this function is context specific. */
      if (debug_flag)
        {
          gint error_count = context->error_count;
          gchar buf[FILTERX_EVAL_ERROR_IDX_FMT_SIZE];

          for (gint err_idx = 0; err_idx < error_count; err_idx++)
            {
              msg_debug("FILTERX ERROR",
                        filterx_eval_format_error_index_tag(err_idx, buf),
                        filterx_eval_format_error_location_tag(err_idx),
                        filterx_eval_format_error_tag(err_idx));
            }
        }

      goto exit;
    }

  if (G_UNLIKELY(context->eval_control_modifier == FXC_DROP))
    result = FXE_DROP;
  else if (filterx_object_truthy(res))
    result = FXE_SUCCESS;
  /* NOTE: we only store the results into the message if the entire evaluation was successful */

exit:
  if (result == FXE_FAILURE)
    _fill_failure_info(context, expr, res);

  _clear_errors(context);
  filterx_object_unref(res);
  filterx_scope_set_dirty(context->scope);
  return result;
}

void
filterx_eval_begin_context(FilterXEvalContext *context,
                           FilterXEvalContext *previous_context,
                           FilterXScope *scope,
                           LogMessage *msg)
{
  filterx_scope_set_message(scope, msg);

  memset(context, 0, sizeof(*context));
  context->msg = msg;
  context->template_eval_options = DEFAULT_TEMPLATE_EVAL_OPTIONS;
  context->scope = scope;

  if (previous_context)
    {
      context->weak_refs = previous_context->weak_refs;
      context->failure_info = previous_context->failure_info;
      context->failure_info_collect_falsy = previous_context->failure_info_collect_falsy;
    }
  else
    context->weak_refs = g_ptr_array_new_full(32, (GDestroyNotify) filterx_object_unref);
  context->previous_context = previous_context;

  context->eval_control_modifier = FXC_UNSET;
  filterx_eval_set_context(context);
}

static void
_failure_info_clear_entry(FilterXFailureInfo *failure_info)
{
  for (gint i = 0; i < failure_info->error_count; i++)
    filterx_error_clear(&failure_info->errors[i]);
  filterx_object_unref(failure_info->meta);
}

static void
_clear_failure_info(GArray *failure_info)
{
  for (guint i = 0; i < failure_info->len; ++i)
    {
      FilterXFailureInfo *fi = &g_array_index(failure_info, FilterXFailureInfo, i);
      _failure_info_clear_entry(fi);
    }

  g_array_set_size(failure_info, 0);
}

void
filterx_eval_end_context(FilterXEvalContext *context)
{
  if (!context->previous_context)
    {
      g_ptr_array_free(context->weak_refs, TRUE);

      if (context->failure_info)
        {
          _clear_failure_info(context->failure_info);
          g_array_free(context->failure_info, TRUE);
        }
    }

  context->failure_info = NULL;
  filterx_object_unref(context->current_frame_meta);
  _clear_errors(context);
  filterx_eval_set_context(context->previous_context);
}

void
filterx_eval_begin_compile(FilterXEvalContext *context, GlobalConfig *cfg)
{
  FilterXConfig *config = filterx_config_get(cfg);

  g_assert(config != NULL);
  memset(context, 0, sizeof(*context));
  context->template_eval_options = DEFAULT_TEMPLATE_EVAL_OPTIONS;
  context->weak_refs = config->weak_refs;
  context->eval_control_modifier = FXC_UNSET;
  filterx_eval_set_context(context);
}

void
filterx_eval_end_compile(FilterXEvalContext *context)
{
  _clear_errors(context);
  filterx_eval_set_context(NULL);
}

void
filterx_eval_enable_failure_info(FilterXEvalContext *context, gboolean collect_falsy)
{
  if (context->failure_info)
    return;

  context->failure_info = g_array_sized_new(FALSE, TRUE, sizeof(FilterXFailureInfo), FAILURE_INFO_PREALLOC_SIZE);
  context->failure_info_collect_falsy = collect_falsy;

  while (context->previous_context)
    {
      context->previous_context->failure_info = context->failure_info;
      context->previous_context->failure_info_collect_falsy = context->failure_info_collect_falsy;

      context = context->previous_context;
    }
}

void
filterx_eval_clear_failure_info(FilterXEvalContext *context)
{
  if (context->failure_info)
    _clear_failure_info(context->failure_info);
}

GArray *
filterx_eval_get_failure_info(FilterXEvalContext *context)
{
  return context->failure_info;
}

EVTTAG *
filterx_eval_format_error_index_tag(gint index, gchar *buf)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  g_assert(context);

  g_snprintf(buf, FILTERX_EVAL_ERROR_IDX_FMT_SIZE, "[%d/%d]", index + 1, context->error_count);
  return evt_tag_str("err_idx", buf);
}

EVTTAG *
filterx_format_eval_result(FilterXEvalResult result)
{
  const gchar *eval_result = NULL;
  switch (result)
    {
    case FXE_SUCCESS:
      eval_result = "matched";
      break;
    case FXE_DROP:
      eval_result = "explicitly dropped";
      break;
    case FXE_FAILURE:
      eval_result = "unmatched";
      break;
    default:
      g_assert_not_reached();
      break;
    }
  return evt_tag_str("result", eval_result);
}

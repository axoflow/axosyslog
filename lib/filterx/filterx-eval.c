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

void
filterx_eval_push_error(const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (context)
    {
      filterx_error_clear(&context->error);
      filterx_error_set_values(&context->error, message, expr, object);
    }
}

void
filterx_eval_push_falsy_error(const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (!context)
    return;

  filterx_error_clear(&context->error);
  filterx_falsy_error_set_values(&context->error, message, expr, object);
}

/* takes ownership of info */
void
filterx_eval_push_error_info(const gchar *message, FilterXExpr *expr, gchar *info, gboolean free_info)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (context)
    {
      filterx_error_clear(&context->error);
      filterx_error_set_values(&context->error, message, expr, NULL);
      filterx_error_set_info(&context->error, info, free_info);
    }
  else
    {
      if (free_info)
        g_free(info);
    }
}

void
filterx_eval_clear_errors(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  filterx_error_clear(&context->error);
}

const gchar *
filterx_eval_get_last_error(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return filterx_error_format(&context->error);
}

EVTTAG *
filterx_eval_format_last_error_tag(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return filterx_error_format_tag(&context->error);
}

EVTTAG *
filterx_eval_format_last_error_location_tag(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return filterx_error_format_location_tag(&context->error);
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

  if (context->error.falsy && !context->failure_info_collect_falsy)
    return;

  FilterXFailureInfo *failure_info = _new_failure_info_entry(context);
  failure_info->meta = filterx_object_ref(context->current_frame_meta);

  if (!context->error.message && context->failure_info_collect_falsy)
    {
      filterx_falsy_error_set_values(&failure_info->error, "Falsy expression", block, block_res);
      return;
    }

  filterx_error_copy(&context->error, &failure_info->error);
}

FilterXEvalResult
filterx_eval_exec(FilterXEvalContext *context, FilterXExpr *expr)
{
  FilterXEvalResult result = FXE_FAILURE;

  FilterXObject *res = filterx_expr_eval(expr);
  if (!res)
    {
      msg_debug("FILTERX ERROR",
                filterx_eval_format_last_error_location_tag(),
                filterx_eval_format_last_error_tag());
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

  filterx_error_clear(&context->error);
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
    context->weak_refs = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_object_unref);
  context->previous_context = previous_context;

  context->eval_control_modifier = FXC_UNSET;
  filterx_eval_set_context(context);
}

static void
_failure_info_clear_entry(FilterXFailureInfo *failure_info)
{
  filterx_error_clear(&failure_info->error);
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

/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include "filterx/filterx-eval.h"
#include "filterx/filterx-expr.h"
#include "logpipe.h"
#include "scratch-buffers.h"
#include "tls-support.h"

TLS_BLOCK_START
{
  FilterXEvalContext *eval_context;
}
TLS_BLOCK_END;

#define eval_context __tls_deref(eval_context)

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
filterx_eval_clear_error(FilterXError *error)
{
  filterx_object_unref(error->object);
  if (error->free_info)
    g_free(error->info);
  memset(error, 0, sizeof(*error));
}

void
filterx_eval_push_error(const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (context)
    {
      filterx_eval_clear_error(&context->error);
      context->error.message = message;
      context->error.expr = expr;
      context->error.object = filterx_object_ref(object);
      context->error.info = NULL;
    }
}

/* takes ownership of info */
void
filterx_eval_push_error_info(const gchar *message, FilterXExpr *expr, gchar *info, gboolean free_info)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (context)
    {
      filterx_eval_clear_error(&context->error);
      context->error.message = message;
      context->error.expr = expr;
      context->error.object = NULL;
      context->error.info = info;
      context->error.free_info = free_info;
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

  filterx_eval_clear_error(&context->error);
}

const gchar *
filterx_eval_get_last_error(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return context->error.message;
}

EVTTAG *
filterx_format_last_error(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (!context->error.message)
    return evt_tag_str("error", "Error information unset");

  const gchar *extra_info = NULL;

  if (context->error.info)
    {
      extra_info = context->error.info;
    }
  else if (context->error.object)
    {
      GString *buf = scratch_buffers_alloc();

      if (!filterx_object_repr(context->error.object, buf))
        {
          LogMessageValueType t;
          if (!filterx_object_marshal(context->error.object, buf, &t))
            g_assert_not_reached();
        }
      extra_info = buf->str;
    }

  return evt_tag_printf("error", "%s%s%s",
                        context->error.message,
                        extra_info ? ": " : "",
                        extra_info ? : "");
}


/*
 * This is not a real weakref implementation as we will never get rid off
 * weak references until the very end of a scope.  If this wasn't the case
 * we would have to:
 *    1) run a proper GC
 *    2) notify weak references once the object is detroyed
 *
 * None of that exists now and I doubt ever will (but never say never).
 * Right now a weak ref is destroyed as a part of the scope finalization
 * process at which point circular references will be broken so the rest can
 * go too.
 */
void
filterx_eval_store_weak_ref(FilterXObject *object)
{
  /* Frozen objects do not need weak refs. */
  if (object && filterx_object_is_frozen(object))
    return;

  FilterXEvalContext *context = filterx_eval_get_context();

  if (object && !object->weak_referenced)
    {
      /* avoid putting object to the list multiple times */
      object->weak_referenced = TRUE;
      g_assert(context->weak_refs);
      g_ptr_array_add(context->weak_refs, filterx_object_ref(object));
    }
}


static gboolean
_evaluate_statement(FilterXExpr *expr)
{
  FilterXObject *res = filterx_expr_eval(expr);
  gboolean success = FALSE;

  if (!res)
    {
      msg_debug("FILTERX ERROR",
                filterx_expr_format_location_tag(expr),
                filterx_format_last_error());
      return FALSE;
    }
  filterx_eval_clear_errors();

  success = expr->ignore_falsy_result || filterx_object_truthy(res);
  if (!success || trace_flag)
    {
      GString *buf = scratch_buffers_alloc();

      if (!filterx_object_repr(res, buf))
        {
          LogMessageValueType t;
          if (!filterx_object_marshal(res, buf, &t))
            g_assert_not_reached();
        }

      if (!success)
        msg_debug("FILTERX FALSY",
                  filterx_expr_format_location_tag(expr),
                  evt_tag_mem("value", buf->str, buf->len),
                  evt_tag_str("type", res->type->name));
      else
        msg_trace("FILTERX ESTEP",
                  filterx_expr_format_location_tag(expr),
                  evt_tag_mem("value", buf->str, buf->len),
                  evt_tag_int("truthy", filterx_object_truthy(res)),
                  evt_tag_str("type", res->type->name));
    }

  filterx_object_unref(res);
  return success;
}

gboolean
filterx_eval_exec_statements(FilterXEvalContext *context, GList *statements, LogMessage *msg)
{
  context->msgs = &msg;
  context->num_msg = 1;
  gboolean success = FALSE;
  for (GList *l = statements; l; l = l->next)
    {
      FilterXExpr *expr = l->data;
      if (!_evaluate_statement(expr))
        {
          goto fail;
        }
    }
  /* NOTE: we only store the results into the message if the entire evaluation was successful */
  success = TRUE;
fail:
  filterx_scope_set_dirty(context->scope);
  return success;
}

void
filterx_eval_init_context(FilterXEvalContext *context, FilterXEvalContext *previous_context)
{
  FilterXScope *scope;

  if (previous_context)
    scope = filterx_scope_ref(previous_context->scope);
  else
    scope = filterx_scope_new();
  filterx_scope_make_writable(&scope);

  memset(context, 0, sizeof(*context));
  context->template_eval_options = DEFAULT_TEMPLATE_EVAL_OPTIONS;
  context->scope = scope;

  if (previous_context)
    context->weak_refs = previous_context->weak_refs;
  else
    context->weak_refs = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_object_unref);
  context->previous_context = previous_context;

  filterx_eval_set_context(context);
}

void
filterx_eval_deinit_context(FilterXEvalContext *context)
{
  if (!context->previous_context)
    g_ptr_array_free(context->weak_refs, TRUE);
  filterx_scope_unref(context->scope);
  filterx_eval_set_context(context->previous_context);
}

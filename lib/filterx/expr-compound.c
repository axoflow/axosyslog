/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/expr-compound.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"
#include "scratch-buffers.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

#include <stdarg.h>

typedef struct _FilterXCompoundExpr
{
  FilterXExpr super;
  /* whether this is a statement expression */
  gboolean return_value_of_last_expr;
  GPtrArray *exprs;

} FilterXCompoundExpr;

static gboolean
_eval_expr(FilterXExpr *expr, FilterXObject **result)
{
  FilterXObject *res = NULL;
  *result = res = filterx_expr_eval(expr);

  if (!res)
    return FALSE;

  gboolean success = expr->ignore_falsy_result || filterx_object_truthy(res);

  if (((!success && debug_flag) || trace_flag) && !expr->suppress_from_trace)
    {
      ScratchBuffersMarker mark;
      GString *buf = scratch_buffers_alloc_and_mark(&mark);
      GString *type_buffer = scratch_buffers_alloc();

      if (res->type == &FILTERX_TYPE_NAME(ref))
        {
          FilterXRef *ref = (FilterXRef *) res;
          g_string_append(type_buffer, "ref/");
          g_string_append(type_buffer, ref->value->type->name);
        }
      else
        g_string_append(type_buffer, res->type->name);

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
                  evt_tag_str("type", type_buffer->str));
      else
        msg_trace("FILTERX ESTEP",
                  filterx_expr_format_location_tag(expr),
                  evt_tag_mem("value", buf->str, buf->len),
                  evt_tag_int("truthy", filterx_object_truthy(res)),
                  evt_tag_str("type", type_buffer->str));
      scratch_buffers_reclaim_marked(mark);
    }
  return success;
}

/* return value indicates if the list of expessions ran through.  *result
 * contains the value of the last expression (even if we bailed out) */
static gboolean
_eval_exprs(FilterXCompoundExpr *self, FilterXObject **result, gsize start_index)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  *result = NULL;
  gsize len = self->exprs->len;
  for (gsize i = start_index; i < len; i++)
    {
      filterx_object_unref(*result);

      FilterXExpr *expr = g_ptr_array_index(self->exprs, i);
      if (!_eval_expr(expr, result))
        return FALSE;

      if (G_UNLIKELY(context->eval_control_modifier != FXC_UNSET))
        {
          /* code flow modifier detected, short circuiting */
          if (context->eval_control_modifier == FXC_BREAK)
            context->eval_control_modifier = FXC_UNSET;

          filterx_object_unref(*result);
          *result = NULL;
          return TRUE;
        }
    }
  /* we exit the loop with a ref to *result, which we return */

  return TRUE;
}

static FilterXObject *
_eval_compound_start(FilterXCompoundExpr *self, gsize start_index)
{
  FilterXObject *result = NULL;

  if (!_eval_exprs(self, &result, start_index))
    {
      if (result)
        {
          filterx_eval_push_error("bailing out due to a falsy expr", &self->super, result);
          filterx_object_unref(result);
          result = NULL;
        }
    }
  else
    {
      if (!result || !self->return_value_of_last_expr)
        {
          filterx_object_unref(result);

          /* an empty list of statements, or a compound statement where the
           * result is ignored.  implicitly TRUE */
          result = filterx_boolean_new(TRUE);
        }
    }

  return result;
}

static FilterXObject *
_eval_compound(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  return _eval_compound_start(self, 0);
}

FilterXObject *
filterx_compound_expr_eval_ext(FilterXExpr *s, gsize start_index)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  return _eval_compound_start(self, start_index);
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  for (gint i = 0; i < self->exprs->len; i++)
    {
      FilterXExpr **expr = (FilterXExpr **) &g_ptr_array_index(self->exprs, i);
      *expr = filterx_expr_optimize(*expr);
    }
  return NULL;
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  for (gint i = 0; i < self->exprs->len; i++)
    {
      FilterXExpr *expr = g_ptr_array_index(self->exprs, i);
      if (!filterx_expr_init(expr, cfg))
        {
          for (gint j = 0; j < i; j++)
            {
              expr = g_ptr_array_index(self->exprs, j);
              filterx_expr_deinit(expr, cfg);
            }
          return FALSE;
        }
    }

  return filterx_expr_init_method(s, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  for (gint i = 0; i < self->exprs->len; i++)
    {
      FilterXExpr *expr = g_ptr_array_index(self->exprs, i);
      filterx_expr_deinit(expr, cfg);
    }

  filterx_expr_deinit_method(s, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  g_ptr_array_free(self->exprs, TRUE);
  filterx_expr_free_method(s);
}

/* Takes reference of expr */

gsize
filterx_compound_expr_get_count(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;
  return self->exprs->len;
}

void
filterx_compound_expr_add(FilterXExpr *s, FilterXExpr *expr)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  g_ptr_array_add(self->exprs, expr);
}

/* Takes reference of expr_list */
void
filterx_compound_expr_add_list(FilterXExpr *s, GList *expr_list)
{
  for (GList *elem = expr_list; elem; elem = elem->next)
    {
      filterx_compound_expr_add(s, elem->data);
    }
  g_list_free(expr_list);
}

FilterXExpr *
filterx_compound_expr_new(gboolean return_value_of_last_expr)
{
  FilterXCompoundExpr *self = g_new0(FilterXCompoundExpr, 1);

  filterx_expr_init_instance(&self->super, "compound");
  self->super.eval = _eval_compound;
  self->super.optimize = _optimize;
  self->super.init = _init;
  self->super.deinit = _deinit;
  self->super.free_fn = _free;
  self->exprs = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_expr_unref);
  self->return_value_of_last_expr = return_value_of_last_expr;

  return &self->super;
}

FilterXExpr *
filterx_compound_expr_new_va(gboolean return_value_of_last_expr, FilterXExpr *first, ...)
{
  FilterXExpr *s = filterx_compound_expr_new(return_value_of_last_expr);
  va_list va;

  va_start(va, first);
  FilterXExpr *arg = first;
  while (arg)
    {
      filterx_compound_expr_add(s, arg);
      arg = va_arg(va, FilterXExpr *);
    }
  va_end(va);
  return s;
}

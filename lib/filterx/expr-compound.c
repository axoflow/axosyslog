/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/expr-compound.h"
#include "filterx/filterx-plist.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"
#include "scratch-buffers.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

#include <stdarg.h>

typedef struct _FilterXCompoundExpr
{
  FilterXExpr super;
  FilterXExprList exprs;
  /* whether this is a statement expression */
  gboolean return_value_of_last_expr;
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
                  evt_tag_str("type", filterx_object_get_type_name(res)));
      else
        msg_trace("FILTERX ESTEP",
                  filterx_expr_format_location_tag(expr),
                  evt_tag_mem("value", buf->str, buf->len),
                  evt_tag_int("truthy", filterx_object_truthy(res)),
                  evt_tag_str("type", filterx_object_get_type_name(res)));
      scratch_buffers_reclaim_marked(mark);
    }

  if (!success)
    filterx_eval_push_falsy_error("bailing out due to a falsy expr", expr, res);

  return success;
}

/* return value indicates if the list of expessions ran through.  *result
 * contains the value of the last expression (even if we bailed out) */
static gboolean
_eval_exprs(FilterXCompoundExpr *self, FilterXObject **result, gsize start_index)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  *result = NULL;
  gsize exprs_len = filterx_expr_list_get_length(&self->exprs);
  for (gsize i = start_index; i < exprs_len; i++)
    {
      filterx_object_unref(*result);

      FilterXExpr *expr = filterx_expr_list_index_fast(&self->exprs, i);
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
      filterx_object_unref(result);
      result = NULL;
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

static gboolean
_optimize_expr(FilterXExpr **pexpr, gpointer user_data)
{
  *pexpr = filterx_expr_optimize(*pexpr);
  return TRUE;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  filterx_expr_list_foreach_ref(&self->exprs, _optimize_expr, NULL);
  return NULL;
}

static gboolean
_invoke_deinit(FilterXExpr *expr, gpointer cfg)
{
  filterx_expr_deinit(expr, (GlobalConfig *) cfg);
  return TRUE;
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  filterx_expr_list_seal(&self->exprs);

  if (!filterx_expr_list_foreach(&self->exprs, (FilterXExprListForeachFunc) filterx_expr_init, cfg))
    {
      filterx_expr_list_foreach(&self->exprs, _invoke_deinit, cfg);
      return FALSE;
    }
  return filterx_expr_init_method(s, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  filterx_expr_list_foreach(&self->exprs, _invoke_deinit, cfg);
  filterx_expr_deinit_method(s, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  filterx_expr_list_clear(&self->exprs);
  filterx_expr_free_method(s);
}

/* Takes reference of expr */

gsize
filterx_compound_expr_get_count(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;
  return filterx_expr_list_get_length(&self->exprs);
}

void
filterx_compound_expr_add_ref(FilterXExpr *s, FilterXExpr *expr)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  filterx_expr_list_add_ref(&self->exprs, expr);
}

/* Takes reference of expr_list */
void
filterx_compound_expr_add_list_ref(FilterXExpr *s, GList *expr_list)
{
  for (GList *elem = expr_list; elem; elem = elem->next)
    {
      filterx_compound_expr_add_ref(s, elem->data);
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
  self->return_value_of_last_expr = return_value_of_last_expr;
  filterx_expr_list_init(&self->exprs);

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
      filterx_compound_expr_add_ref(s, arg);
      arg = va_arg(va, FilterXExpr *);
    }
  va_end(va);
  return s;
}

/*
 * Copyright (c) 2023 shifter <shifter@axoflow.com>
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

#include "filterx/expr-condition.h"
#include "filterx/object-primitive.h"

typedef struct _FilterXConditional FilterXConditional;

struct _FilterXConditional
{
  FilterXExpr super;
  FilterXExpr *condition;
  FilterXExpr *true_branch;
  FilterXExpr *false_branch;
};

static void
_free(FilterXExpr *s)
{
  FilterXConditional *self = (FilterXConditional *) s;

  filterx_expr_unref(self->condition);
  filterx_expr_unref(self->true_branch);
  filterx_expr_unref(self->false_branch);
  filterx_expr_free_method(s);
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXConditional *self = (FilterXConditional *) s;
  FilterXObject *condition_value = filterx_expr_eval(self->condition);
  FilterXObject *result = NULL;

  if (!condition_value)
    return NULL;

  if (filterx_object_truthy(condition_value))
    {
      if (self->true_branch)
        result = filterx_expr_eval(self->true_branch);
      else
        result = filterx_object_ref(condition_value);
    }
  else
    {
      if (self->false_branch)
        result = filterx_expr_eval(self->false_branch);
      else
        result = filterx_boolean_new(TRUE);
    }

  filterx_object_unref(condition_value);
  return result;
}

void
filterx_conditional_set_true_branch(FilterXExpr *s, FilterXExpr *true_branch)
{
  FilterXConditional *self = (FilterXConditional *) s;

  filterx_expr_unref(self->true_branch);
  self->true_branch = true_branch;
}

void
filterx_conditional_set_false_branch(FilterXExpr *s, FilterXExpr *false_branch)
{
  FilterXConditional *self = (FilterXConditional *) s;

  filterx_expr_unref(self->false_branch);
  self->false_branch = false_branch;
}

FilterXExpr *
filterx_conditional_new(FilterXExpr *condition)
{
  FilterXConditional *self = g_new0(FilterXConditional, 1);
  filterx_expr_init_instance(&self->super);
  self->super.eval = _eval;
  self->super.free_fn = _free;
  self->condition = condition;
  return &self->super;
}

FilterXExpr *
filterx_conditional_find_tail(FilterXExpr *s)
{
  /* check if this is a FilterXConditional instance */
  if (s->eval != _eval)
    return NULL;

  FilterXConditional *self = (FilterXConditional *) s;
  if (self->false_branch)
    {
      FilterXConditional *tail = (FilterXConditional *) filterx_conditional_find_tail(self->false_branch);
      if (tail)
        {
          g_assert(tail->false_branch == NULL);
          return &tail->super;
        }
    }
  return s;
}

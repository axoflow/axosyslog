/*
 * Copyright (c) 2023 shifter <shifter@axoflow.com>
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

#include "filterx/expr-conditional.h"
#include "filterx/object-primitive.h"

static FilterXConditional *
_tail_condition(FilterXConditional *c)
{
  g_assert(c != NULL);
  if (c->false_branch != NULL)
    return _tail_condition(c->false_branch);
  else
    return c;
}

static gboolean
_has_condition(FilterXConditional *self)
{
  return self->condition != FILTERX_CONDITIONAL_NO_CONDITION;
}

static FilterXObject *
_eval_statements(FilterXConditional *self)
{
  g_assert(self->statements);

  /*
   * We propagate both the valid and error results
   * from filterx_expr_list_eval().
   */
  FilterXObject *result;
  filterx_expr_list_eval(self->statements, &result);
  return result;
}

static FilterXObject *
_eval_with_condition(FilterXConditional *self)
{
  FilterXObject *condition_value = filterx_expr_eval(self->condition);
  if (!condition_value)
    return NULL;

  if (filterx_object_truthy(condition_value))
    {
      if (self->statements)
        {
          filterx_object_unref(condition_value);
          return _eval_statements(self);
        }

      /*
       * Return the condition's value to make it work
       * with ternary expressions without a true value.
       * E.g.: foo ? : bar;
       */
      return condition_value;
    }

  filterx_object_unref(condition_value);

  if (self->false_branch)
    return filterx_expr_eval(&self->false_branch->super);

  /*
   * The condition is falsy, and we don't have a false branch.
   * This implies we are not in a ternary expression.
   * Return true so we don't block the flow.
   */
  return filterx_boolean_new(TRUE);
}

/* else branch */
static FilterXObject *
_eval_without_condition(FilterXConditional *self)
{
  if (self->statements)
    return _eval_statements(self);

  /* No statements is an implicit true. */
  return filterx_boolean_new(TRUE);
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXConditional *self = (FilterXConditional *) s;

  if (_has_condition(self))
    return _eval_with_condition(self);
  return _eval_without_condition(self);
}

static void
_free (FilterXExpr *s)
{
  FilterXConditional *self = (FilterXConditional *) s;

  filterx_expr_unref(self->condition);
  g_list_free_full(self->statements, (GDestroyNotify) filterx_expr_unref);
  if (self->false_branch != NULL)
    filterx_expr_unref(&self->false_branch->super);
  filterx_expr_free_method(s);
}

FilterXExpr *
filterx_conditional_new_conditional_codeblock(FilterXExpr *condition, GList *stmts)
{
  FilterXConditional *self = g_new0(FilterXConditional, 1);
  filterx_expr_init_instance(&self->super);
  self->super.eval = _eval;
  self->super.free_fn = _free;
  self->condition = condition;
  self->statements = stmts;
  return &self->super;
}

FilterXExpr *
filterx_conditional_new_codeblock(GList *stmts)
{
  return filterx_conditional_new_conditional_codeblock(FILTERX_CONDITIONAL_NO_CONDITION, stmts);
}


FilterXExpr *
filterx_conditional_add_false_branch(FilterXConditional *s, FilterXConditional *false_branch)
{
  g_assert(s != NULL);
  FilterXConditional *last_condition = _tail_condition(s);

  // avoid to create nested elses (if () {} else {} else {} else {})
  g_assert(last_condition->condition);

  last_condition->false_branch = false_branch;
  return &s->super;
}

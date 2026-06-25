/*
 * Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "expr-plus-assign.h"
#include "expr-variable.h"
#include "filterx-eval.h"

typedef struct FilterXOperatorPlusAssign
{
  FilterXBinaryOp super;
} FilterXOperatorPlusAssign;

static FilterXObject *
_eval_plus_assign(FilterXExpr *s)
{
  FilterXOperatorPlusAssign *self = (FilterXOperatorPlusAssign *) s;

  FilterXObject *rhs_object = filterx_expr_eval_typed(self->super.rhs);
  if (!rhs_object)
    {
      filterx_eval_push_error_static_info("Failed to add values in place", s, "Failed to evaluate right hand side");
      return NULL;
    }

  FilterXObject *res = filterx_expr_plus_assign(self->super.lhs, rhs_object);
  filterx_object_unref(rhs_object);
  if (!res)
    {
      filterx_eval_push_error_static_info("Failed to add values in place", s, "plus_assign() method failed");
      return NULL;
    }

  return res;
}

static void
_plus_assign_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  FilterXOperatorPlusAssign *self = (FilterXOperatorPlusAssign *) s;

  filterx_expr_infer_types(self->super.rhs, env);
  filterx_expr_infer_types(self->super.lhs, env);

  FilterXStaticTypeSpec rhs_spec = self->super.rhs ? self->super.rhs->static_type : INITIAL_FILTERX_STATIC_TYPE_SPEC;

  /* For string+=string / list+=list / dict+=dict the result type matches; for any mismatch
   * (or unknown side) we collapse to UNKNOWN. The LHS env entry takes the same meet. */
  FilterXVariableHandle handle;
  if (filterx_variable_expr_get_handle(self->super.lhs, &handle))
    {
      FilterXStaticTypeSpec prior = filterx_type_env_get(env, handle);
      FilterXStaticTypeSpec result = filterx_static_type_spec_meet(prior, rhs_spec);
      filterx_type_env_set(env, handle, result);
      s->static_type = result;
    }
  else
    {
      s->static_type = INITIAL_FILTERX_STATIC_TYPE_SPEC;
    }
}

FilterXExpr *
filterx_operator_plus_assign_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXOperatorPlusAssign *self = g_new0(FilterXOperatorPlusAssign, 1);
  filterx_binary_op_init_instance(&self->super, "plus-assign", FXE_WRITE, lhs, rhs);
  self->super.super.eval = _eval_plus_assign;
  self->super.super.infer_types = _plus_assign_infer_types;
  self->super.super.ignore_falsy_result = TRUE;

  return &self->super.super;
}

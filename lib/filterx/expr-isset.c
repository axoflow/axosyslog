/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/expr-isset.h"
#include "filterx/object-primitive.h"

static FilterXObject *
_eval_isset(FilterXExpr *s)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;

  return filterx_boolean_new(filterx_expr_is_set(self->operand));
}

FilterXExpr *
filterx_isset_new(FilterXExpr *expr)
{
  FilterXUnaryOp *self = g_new0(FilterXUnaryOp, 1);
  filterx_unary_op_init_instance(self, "isset", FALSE, expr);
  self->super.eval = _eval_isset;
  return &self->super;
}

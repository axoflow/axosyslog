/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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


#include "filterx/expr-break.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"

static FilterXObject *
_eval_break(FilterXExpr *s)
{
  FilterXEvalContext *context = filterx_eval_get_context();
  context->eval_control_modifier = FXC_BREAK;

  return filterx_boolean_new(TRUE);
}

FilterXExpr *
filterx_expr_break(void)
{
  FilterXExpr *self = g_new0(FilterXExpr, 1);
  filterx_expr_init_instance(self, "break");
  self->eval = _eval_break;

  return self;
}

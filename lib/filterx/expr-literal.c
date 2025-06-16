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
#include "filterx/expr-literal.h"

typedef struct _FilterXLiteral
{
  FilterXExpr super;
  FilterXObject *object;
} FilterXLiteral;

static FilterXObject *
_eval_literal(FilterXExpr *s)
{
  FilterXLiteral *self = (FilterXLiteral *) s;
  return filterx_object_ref(self->object);
}

static void
_free(FilterXExpr *s)
{
  FilterXLiteral *self = (FilterXLiteral *) s;
  filterx_object_unref(self->object);
  filterx_expr_free_method(s);
}

/* NOTE: takes the object reference */
void
filterx_literal_init_instance(FilterXLiteral *s, FilterXObject *object)
{
  FilterXLiteral *self = (FilterXLiteral *) s;

  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(literal));
  self->super.eval = _eval_literal;
  self->super.free_fn = _free;
  self->object = object;
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_literal_new(FilterXObject *object)
{
  FilterXLiteral *self = g_new0(FilterXLiteral, 1);

  filterx_literal_init_instance(self, object);
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(literal);

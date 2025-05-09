/*
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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
#include "filterx/expr-membership.h"
#include "filterx/object-primitive.h"

typedef struct FilterXOperatorIn_
{
  FilterXBinaryOp super;
} FilterXOperatorIn;

static FilterXObject *
_eval_in(FilterXExpr *s)
{
  return filterx_boolean_new(TRUE);
}

static FilterXExpr *
_optimize_in(FilterXExpr *s)
{
  return NULL;
}

static void
_free_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;

  filterx_binary_op_free_method(&self->super.super);
}

FilterXExpr *
filterx_membership_in_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXOperatorIn *self = g_new0(FilterXOperatorIn, 1);

  filterx_binary_op_init_instance(&self->super, "in", lhs, rhs);
  self->super.super.optimize = _optimize_in;
  self->super.super.eval = _eval_in;
  self->super.super.free_fn = _free_in;

  return &self->super.super;
}

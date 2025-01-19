/*
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
 * Copyright (c) 2025 Axoflow
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
#ifndef FILTERX_SWITCH_H_INCLUDED
#define FILTERX_SWITCH_H_INCLUDED

#include "filterx-expr.h"


/* the switch statement */
FilterXExpr *filterx_switch_new(FilterXExpr *selector, GList *body);

/* a case in the switch statement */
FilterXExpr *filterx_switch_case_new(FilterXExpr *value);

FILTERX_EXPR_DECLARE_TYPE(switch);
FILTERX_EXPR_DECLARE_TYPE(switch_case);

static inline gboolean
filterx_expr_is_switch_case(FilterXExpr *expr)
{
  return expr && expr->type == FILTERX_EXPR_TYPE_NAME(switch_case);
}

static inline gboolean
filterx_expr_is_switch(FilterXExpr *expr)
{
  return expr && expr->type == FILTERX_EXPR_TYPE_NAME(switch);
}


#endif

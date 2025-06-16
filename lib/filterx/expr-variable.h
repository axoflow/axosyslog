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
#ifndef FILTERX_EXPR_VARIABLE_H_INCLUDED
#define FILTERX_EXPR_VARIABLE_H_INCLUDED

#include "filterx/filterx-expr.h"
#include "filterx/filterx-config.h"
#include "filterx/object-string.h"
#include "cfg.h"

FilterXExpr *filterx_msg_variable_expr_new(const gchar *name);
FilterXExpr *filterx_floating_variable_expr_new(const gchar *name);
void filterx_variable_expr_declare(FilterXExpr *s);


FILTERX_EXPR_DECLARE_TYPE(variable);

static inline gboolean
filterx_expr_is_variable(FilterXExpr *expr)
{
  return expr && expr->type == FILTERX_EXPR_TYPE_NAME(variable);
}

#endif

/*
 * Copyright (c) 2024 Attila Szakacs
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
#ifndef FILTERX_COMPOUND_H_INCLUDED
#define FILTERX_COMPOUND_H_INCLUDED

#include "filterx/filterx-expr.h"

void filterx_compound_expr_add(FilterXExpr *s, FilterXExpr *expr);
void filterx_compound_expr_add_list(FilterXExpr *s, GList *expr_list);
FilterXExpr *filterx_compound_expr_new(gboolean return_value_of_last_expr);
FilterXExpr *filterx_compound_expr_new_va(gboolean return_value_of_last_expr, FilterXExpr *first, ...);

#endif

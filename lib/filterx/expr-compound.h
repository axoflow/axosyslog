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
#ifndef FILTERX_COMPOUND_H_INCLUDED
#define FILTERX_COMPOUND_H_INCLUDED

#include "filterx/filterx-expr.h"

FilterXObject *filterx_compound_expr_eval_ext(FilterXExpr *s, gsize start_index);
void filterx_compound_expr_add_ref(FilterXExpr *s, FilterXExpr *expr);
void filterx_compound_expr_add_list_ref(FilterXExpr *s, GList *expr_list);
FilterXExpr *filterx_compound_expr_new(gboolean return_value_of_last_expr);
FilterXExpr *filterx_compound_expr_new_va(gboolean return_value_of_last_expr, FilterXExpr *first, ...);
gsize filterx_compound_expr_get_count(FilterXExpr *s);

#endif

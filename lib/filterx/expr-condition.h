/*
 * Copyright (c) 2023 shifter <shifter@axoflow.com>
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

#ifndef FILTERX_CONDITION_H_INCLUDED
#define FILTERX_CONDITION_H_INCLUDED

#include "filterx/filterx-expr.h"

void filterx_conditional_set_true_branch(FilterXExpr *s, FilterXExpr *true_branch);
void filterx_conditional_set_false_branch(FilterXExpr *s, FilterXExpr *false_branch);
FilterXExpr *filterx_conditional_find_tail(FilterXExpr *s);
FilterXExpr *filterx_conditional_new(FilterXExpr *condition);

#endif

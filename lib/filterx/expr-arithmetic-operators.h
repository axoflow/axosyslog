/*
 * Copyright (c) 2024 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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
#ifndef FILTERX_EXPR_ARITHMETIC_OPERATORS_H_INCLUDED
#define FILTERX_EXPR_ARITHMETIC_OPERATORS_H_INCLUDED

#include "filterx-expr.h"

FilterXExpr *filterx_arithmetic_operator_substraction_new(FilterXExpr *lhs, FilterXExpr *rhs);
FilterXExpr *filterx_arithmetic_operator_multiplication_new(FilterXExpr *lhs, FilterXExpr *rhs);
FilterXExpr *filterx_arithmetic_operator_division_new(FilterXExpr *lhs, FilterXExpr *rhs);
FilterXExpr *filterx_arithmetic_operator_modulo_new(FilterXExpr *lhs, FilterXExpr *rhs);
FilterXExpr *filterx_arithmetic_operator_uminus_new(FilterXExpr *operand);

#endif

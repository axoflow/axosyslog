/*
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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
#ifndef FILTERX_ARITHMETIC_OPERATORS_DEVIRT_H_INCLUDED
#define FILTERX_ARITHMETIC_OPERATORS_DEVIRT_H_INCLUDED

#include "filterx/filterx-expr.h"

#if SYSLOG_NG_ENABLE_JIT
/* Defined in expr-arithmetic-operators.c. */
FilterXIRValue _compile_binary_arithmetic(FilterXExpr *s, FilterXJIT *jit, const gchar *fn_name);

FilterXIRValue _compile_plus(FilterXExpr *s, FilterXJIT *jit);

/* @lhs must hold a non-NULL FilterXString, @rhs any string-extractable object. The emitted
 * code consumes both operand references. */
FilterXIRValue filterx_string_concat_compile(FilterXJIT *jit, FilterXIRValue lhs, FilterXIRValue rhs,
                                             FilterXExpr *expr);
#endif

#endif

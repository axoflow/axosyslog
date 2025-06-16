/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Szilard Parrag
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

#ifndef FILTERX_FUNC_SDATA_H_INCLUDED
#define FILTERX_FUNC_SDATA_H_INCLUDED

#include "filterx/expr-function.h"

FilterXExpr *filterx_function_is_sdata_from_enterprise_new(FilterXFunctionArgs *args, GError **error);
FilterXObject *filterx_simple_function_has_sdata(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_simple_function_get_sdata(FilterXExpr *s, FilterXObject *args[], gsize args_len);

#endif

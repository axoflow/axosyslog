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
#ifndef FILTERX_GLOBALS_H_INCLUDED
#define FILTERX_GLOBALS_H_INCLUDED

#include "filterx-object.h"
#include "filterx/expr-function.h"

void filterx_global_init(void);
void filterx_global_deinit(void);

// Builtin functions
FilterXSimpleFunctionProto filterx_builtin_simple_function_lookup(const gchar *);
FilterXFunctionCtor filterx_builtin_function_ctor_lookup(const gchar *function_name);
gboolean filterx_builtin_function_exists(const gchar *function_name);

// FilterX types
FilterXType *filterx_type_lookup(const gchar *type_name);
gboolean filterx_type_register(const gchar *type_name, FilterXType *fxtype);

// Helpers
FilterXObject *filterx_typecast_get_arg(FilterXExpr *s, FilterXObject *args[], gsize args_len);

#endif

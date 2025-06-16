/*
 * Copyright (c) 2024 shifter <shifter@axoflow.com>
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
#ifndef FILTERX_PRIVATE_H_INCLUDED
#define FILTERX_PRIVATE_H_INCLUDED

#include "filterx-object.h"
#include "filterx/expr-function.h"

// Builtin functions
FilterXExpr *filterx_simple_function_new(const gchar *function_name, FilterXFunctionArgs *args,
                                         FilterXSimpleFunctionProto function_proto, GError **error);

gboolean filterx_builtin_simple_function_register_private(GHashTable *ht, const gchar *fn_name,
                                                          FilterXSimpleFunctionProto func);
FilterXSimpleFunctionProto filterx_builtin_simple_function_lookup_private(GHashTable *ht, const gchar *fn_name);
void filterx_builtin_simple_functions_init_private(GHashTable **ht);
void filterx_builtin_simple_functions_deinit_private(GHashTable *ht);

gboolean filterx_builtin_function_ctor_register_private(GHashTable *ht, const gchar *fn_name, FilterXFunctionCtor ctor);
FilterXFunctionCtor filterx_builtin_function_ctor_lookup_private(GHashTable *ht, const gchar *fn_name);
void filterx_builtin_function_ctors_init_private(GHashTable **ht);
void filterx_builtin_function_ctors_deinit_private(GHashTable *ht);

// Types
gboolean filterx_type_register_private(GHashTable *ht, const gchar *type_name, FilterXType *fxtype);
FilterXType *filterx_type_lookup_private(GHashTable *ht, const gchar *type_name);
void filterx_types_init_private(GHashTable **ht);
void filterx_types_deinit_private(GHashTable *ht);

#endif

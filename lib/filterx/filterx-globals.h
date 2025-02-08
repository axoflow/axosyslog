/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#ifndef FILTERX_GLOBALS_H_INCLUDED
#define FILTERX_GLOBALS_H_INCLUDED

#include "filterx-object.h"
#include "filterx/expr-function.h"

#define FILTERX_BOOL_CACHE_LIMIT 2
#define FILTERX_INTEGER_CACHE_LIMIT 100

/* cache indices */
enum
{
  FILTERX_STRING_ZERO_LENGTH,
  FILTERX_STRING_NUMBER0,
  FILTERX_STRING_NUMBER1,
  FILTERX_STRING_NUMBER2,
  FILTERX_STRING_NUMBER3,
  FILTERX_STRING_NUMBER4,
  FILTERX_STRING_NUMBER5,
  FILTERX_STRING_NUMBER6,
  FILTERX_STRING_NUMBER7,
  FILTERX_STRING_NUMBER8,
  FILTERX_STRING_NUMBER9,
  FILTERX_STRING_MAX,
};

typedef struct _FilterXGlobalCache
{
  FilterXObject *bool_cache[FILTERX_BOOL_CACHE_LIMIT];
  FilterXObject *integer_cache[FILTERX_INTEGER_CACHE_LIMIT];
  FilterXObject *string_cache[FILTERX_STRING_MAX];
} FilterXGlobalCache;

extern FilterXGlobalCache global_cache;

void filterx_cache_object(FilterXObject **cache_slot, FilterXObject *object);
void filterx_uncache_object(FilterXObject **cache_slot);

void filterx_global_init(void);
void filterx_global_deinit(void);

// Builtin functions
FilterXSimpleFunctionProto filterx_builtin_simple_function_lookup(const gchar *);
FilterXFunctionCtor filterx_builtin_function_ctor_lookup(const gchar *function_name);
FilterXFunctionCtor filterx_builtin_generator_function_ctor_lookup(const gchar *function_name);
gboolean filterx_builtin_function_exists(const gchar *function_name);
gboolean filterx_builtin_generator_function_exists(const gchar *function_name);

// FilterX types
FilterXType *filterx_type_lookup(const gchar *type_name);
gboolean filterx_type_register(const gchar *type_name, FilterXType *fxtype);

// Helpers
FilterXObject *filterx_typecast_get_arg(FilterXExpr *s, FilterXObject *args[], gsize args_len);

#endif

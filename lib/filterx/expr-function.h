/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023-2024 Attila Szakacs <attila.szakacs@axoflow.com>
 * Copyright (c) 2023 shifter
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

#ifndef FILTERX_EXPR_FUNCTION_H_INCLUDED
#define FILTERX_EXPR_FUNCTION_H_INCLUDED

#include "filterx/filterx-expr.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-function-args.h"
#include "filterx/expr-generator.h"
#include "stats/stats-registry.h"
#include "generic-number.h"
#include "plugin.h"

typedef FilterXObject *(*FilterXSimpleFunctionProto)(FilterXExpr *s, FilterXObject *args[], gsize args_len);

void filterx_simple_function_argument_error(FilterXExpr *s, gchar *error_info);

static inline void
filterx_simple_function_free_args(FilterXObject *args[], gsize args_len)
{
  for (gsize i = 0; i < args_len; i++)
    filterx_object_unref(args[i]);
}

typedef struct _FilterXFunction
{
  FilterXExpr super;
  gchar *function_name;
} FilterXFunction;

typedef struct _FilterXGeneratorFunction
{
  FilterXExprGenerator super;
  gchar *function_name;
} FilterXGeneratorFunction;


typedef FilterXExpr *(*FilterXFunctionCtor)(FilterXFunctionArgs *, GError **);

void filterx_function_init_instance(FilterXFunction *s, const gchar *function_name);
FilterXExpr *filterx_function_optimize_method(FilterXFunction *s);
gboolean filterx_function_init_method(FilterXFunction *s, GlobalConfig *cfg);
void filterx_function_deinit_method(FilterXFunction *s, GlobalConfig *cfg);
void filterx_function_free_method(FilterXFunction *s);

FilterXExpr *filterx_generator_function_optimize_method(FilterXGeneratorFunction *s);
gboolean filterx_generator_function_init_method(FilterXGeneratorFunction *s, GlobalConfig *cfg);
void filterx_generator_function_deinit_method(FilterXGeneratorFunction *s, GlobalConfig *cfg);
void filterx_generator_function_init_instance(FilterXGeneratorFunction *s, const gchar *function_name);
void filterx_generator_function_free_method(FilterXGeneratorFunction *s);

FilterXExpr *filterx_function_lookup(GlobalConfig *cfg, const gchar *function_name, GList *args, GError **error);
FilterXExpr *filterx_generator_function_lookup(GlobalConfig *cfg, const gchar *function_name, GList *args,
                                               GError **error);


#define _FILTERX_FUNCTION_PLUGIN(func_name, func_type) \
  { \
    .type = func_type, \
    .name = # func_name, \
    .construct = filterx_function_ ## func_name ## _construct, \
  }

#define FILTERX_FUNCTION_PROTOTYPE(func_name)                \
  gpointer                                                   \
  filterx_function_ ## func_name ## _construct(Plugin *self)
#define FILTERX_FUNCTION_DECLARE(func_name) FILTERX_FUNCTION_PROTOTYPE(func_name);
#define FILTERX_FUNCTION(func_name, ctor) \
  FILTERX_FUNCTION_PROTOTYPE(func_name)   \
  {                                       \
    FilterXFunctionCtor f = ctor;         \
    return (gpointer) f;                  \
  }
#define FILTERX_FUNCTION_PLUGIN(func_name) _FILTERX_FUNCTION_PLUGIN(func_name, LL_CONTEXT_FILTERX_FUNC)


#define FILTERX_SIMPLE_FUNCTION_DECLARE(func_name) FILTERX_FUNCTION_DECLARE(func_name)
#define FILTERX_SIMPLE_FUNCTION(func_name, call) \
  FILTERX_FUNCTION_PROTOTYPE(func_name)   \
  {                                              \
    FilterXSimpleFunctionProto f = call;         \
    return (gpointer) f;                         \
  }
#define FILTERX_SIMPLE_FUNCTION_PLUGIN(func_name) _FILTERX_FUNCTION_PLUGIN(func_name, LL_CONTEXT_FILTERX_SIMPLE_FUNC)


#define FILTERX_GENERATOR_FUNCTION_DECLARE(func_name) FILTERX_FUNCTION_DECLARE(func_name)
#define FILTERX_GENERATOR_FUNCTION(func_name, ctor) FILTERX_FUNCTION(func_name, ctor)
#define FILTERX_GENERATOR_FUNCTION_PLUGIN(func_name) _FILTERX_FUNCTION_PLUGIN(func_name, LL_CONTEXT_FILTERX_GEN_FUNC)

#endif

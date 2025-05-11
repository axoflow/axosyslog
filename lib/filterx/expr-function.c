/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023 Balázs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/expr-function.h"
#include "filterx/filterx-grammar.h"
#include "filterx/filterx-globals.h"
#include "filterx/filterx-eval.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "plugin.h"
#include "cfg.h"
#include "mainloop.h"
#include "stats/stats-cluster-single.h"

typedef struct _FilterXSimpleFunction
{
  FilterXFunction super;
  GPtrArray *args;
  FilterXSimpleFunctionProto function_proto;
} FilterXSimpleFunction;


void
filterx_simple_function_argument_error(FilterXExpr *s, gchar *error_info)
{
  FilterXSimpleFunction *self = (FilterXSimpleFunction *) s;

  filterx_eval_push_error_static_info(self ? self->super.function_name : "n/a", s, error_info);
}

static FilterXObject *
_get_arg_object(FilterXSimpleFunction *self, guint64 index)
{
  FilterXExpr *expr = g_ptr_array_index(self->args, index);
  if (!expr)
    return NULL;

  return filterx_expr_eval_typed(expr);
}

static gboolean
_simple_function_eval_args(FilterXSimpleFunction *self, FilterXObject **args, gsize *args_len)
{
  gsize n = *args_len;
  for (gsize i = 0; i < n; i++)
    {
      if ((args[i] = _get_arg_object(self, i)) == NULL)
        {
          *args_len = i;
          return FALSE;
        }
    }
  *args_len = n;
  return TRUE;
}

static void
_simple_function_free_args(FilterXObject *args[], gsize args_len)
{
  for (gsize i = 0; i < args_len; i++)
    filterx_object_unref(args[i]);
}

static FilterXObject *
_simple_eval(FilterXExpr *s)
{
  FilterXSimpleFunction *self = (FilterXSimpleFunction *) s;
  gsize args_len = self->args->len;
  FilterXObject *args[self->args->len];
  FilterXObject *res = NULL;

  if (_simple_function_eval_args(self, args, &args_len))
    {
      res = self->function_proto(s, args, args_len);
    }

  _simple_function_free_args(args, args_len);
  return res;
}

static FilterXExpr *
_simple_optimize(FilterXExpr *s)
{
  FilterXSimpleFunction *self = (FilterXSimpleFunction *) s;

  for (guint64 i = 0; i < self->args->len; i++)
    {
      FilterXExpr **arg = (FilterXExpr **) &g_ptr_array_index(self->args, i);
      *arg = filterx_expr_optimize(*arg);
    }
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_simple_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXSimpleFunction *self = (FilterXSimpleFunction *) s;

  for (guint64 i = 0; i < self->args->len; i++)
    {
      FilterXExpr *arg = g_ptr_array_index(self->args, i);
      if (!filterx_expr_init(arg, cfg))
        {
          for (gint j = 0; j < i; j++)
            {
              arg = g_ptr_array_index(self->args, j);
              filterx_expr_deinit(arg, cfg);
            }
          return FALSE;
        }
    }

  return filterx_function_init_method(&self->super, cfg);
}

static void
_simple_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXSimpleFunction *self = (FilterXSimpleFunction *) s;

  for (guint64 i = 0; i < self->args->len; i++)
    {
      FilterXExpr *arg = g_ptr_array_index(self->args, i);
      filterx_expr_deinit(arg, cfg);
    }

  filterx_function_deinit_method(&self->super, cfg);
}

static void
_simple_free(FilterXExpr *s)
{
  FilterXSimpleFunction *self = (FilterXSimpleFunction *) s;
  g_ptr_array_free(self->args, TRUE);
  filterx_function_free_method(&self->super);
}

static inline gboolean
_simple_process_args(FilterXSimpleFunction *self, FilterXFunctionArgs *args, GError **error)
{
  guint64 len = filterx_function_args_len(args);

  for (guint64 i = 0; i < len; i++)
    {
      FilterXExpr *expr = filterx_function_args_get_expr(args, i);
      if (!expr)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_UNEXPECTED_ARGS, "can't get argument");
          return FALSE;
        }

      g_ptr_array_add(self->args, expr);
    }

  return TRUE;
}

FilterXExpr *
filterx_simple_function_new(const gchar *function_name, FilterXFunctionArgs *args,
                            FilterXSimpleFunctionProto function_proto, GError **error)
{
  FilterXSimpleFunction *self = g_new0(FilterXSimpleFunction, 1);

  filterx_function_init_instance(&self->super, function_name);
  self->super.super.eval = _simple_eval;
  self->super.super.optimize = _simple_optimize;
  self->super.super.init = _simple_init;
  self->super.super.deinit = _simple_deinit;
  self->super.super.free_fn = _simple_free;
  self->function_proto = function_proto;

  self->args = g_ptr_array_new_full(filterx_function_args_len(args), (GDestroyNotify) filterx_expr_unref);
  if (!_simple_process_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FilterXExpr *
filterx_function_optimize_method(FilterXFunction *s)
{
  return NULL;
}

gboolean
filterx_function_init_method(FilterXFunction *s, GlobalConfig *cfg)
{
  return filterx_expr_init_method(&s->super, cfg);
}

void
filterx_function_deinit_method(FilterXFunction *s, GlobalConfig *cfg)
{
  filterx_expr_deinit_method(&s->super, cfg);
}

void
filterx_function_free_method(FilterXFunction *s)
{
  g_free(s->function_name);
  filterx_expr_free_method(&s->super);
}

static inline FilterXExpr *
_function_optimize(FilterXExpr *s)
{
  FilterXFunction *self = (FilterXFunction *) s;
  return filterx_function_optimize_method(self);
}

static inline gboolean
_function_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunction *self = (FilterXFunction *) s;
  return filterx_function_init_method(self, cfg);
}

static inline void
_function_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunction *self = (FilterXFunction *) s;
  return filterx_function_deinit_method(self, cfg);
}

static inline void
_function_free(FilterXExpr *s)
{
  FilterXFunction *self = (FilterXFunction *) s;
  filterx_function_free_method(self);
}

void
filterx_function_init_instance(FilterXFunction *s, const gchar *function_name)
{
  filterx_expr_init_instance(&s->super, "function");
  s->function_name = g_strdup_printf("%s()", function_name);
  s->super.optimize = _function_optimize;
  s->super.init = _function_init;
  s->super.deinit = _function_deinit;
  s->super.name = s->function_name;
  s->super.free_fn = _function_free;
}

FilterXExpr *
filterx_generator_function_optimize_method(FilterXGeneratorFunction *s)
{
  return filterx_generator_optimize_method(&s->super.super);
}

gboolean
filterx_generator_function_init_method(FilterXGeneratorFunction *s, GlobalConfig *cfg)
{
  return filterx_generator_init_method(&s->super.super, cfg);
}

void
filterx_generator_function_deinit_method(FilterXGeneratorFunction *s, GlobalConfig *cfg)
{
  filterx_generator_deinit_method(&s->super.super, cfg);
}

void
filterx_generator_function_free_method(FilterXGeneratorFunction *s)
{
  g_free(s->function_name);
  filterx_generator_free_method(&s->super.super);
}

static inline FilterXExpr *
_generator_function_optimize(FilterXExpr *s)
{
  FilterXGeneratorFunction *self = (FilterXGeneratorFunction *) s;
  return filterx_generator_function_optimize_method(self);
}

static inline gboolean
_generator_function_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXGeneratorFunction *self = (FilterXGeneratorFunction *) s;
  return filterx_generator_function_init_method(self, cfg);
}

static inline void
_generator_function_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXGeneratorFunction *self = (FilterXGeneratorFunction *) s;
  filterx_generator_function_deinit_method(self, cfg);
}

static inline void
_generator_function_free(FilterXExpr *s)
{
  FilterXGeneratorFunction *self = (FilterXGeneratorFunction *) s;
  filterx_generator_function_free_method(self);
}

void
filterx_generator_function_init_instance(FilterXGeneratorFunction *s, const gchar *function_name)
{
  filterx_generator_init_instance(&s->super.super);
  s->function_name = g_strdup_printf("%s()", function_name);
  s->super.super.optimize = _generator_function_optimize;
  s->super.super.init = _generator_function_init;
  s->super.super.deinit = _generator_function_deinit;
  s->super.super.free_fn = _generator_function_free;
}

static FilterXExpr *
_lookup_simple_function(GlobalConfig *cfg, const gchar *function_name, FilterXFunctionArgs *args, GError **error)
{
  // Checking filterx builtin functions first
  FilterXSimpleFunctionProto f = filterx_builtin_simple_function_lookup(function_name);
  if (f != NULL)
    {
      return filterx_simple_function_new(function_name, args, f, error);
    }

  // fallback to plugin lookup
  Plugin *p = cfg_find_plugin(cfg, LL_CONTEXT_FILTERX_SIMPLE_FUNC, function_name);

  if (p == NULL)
    return NULL;

  f = plugin_construct(p);
  if (f == NULL)
    return NULL;

  return filterx_simple_function_new(function_name, args, f, error);
}

static FilterXExpr *
_lookup_function(GlobalConfig *cfg, const gchar *function_name, FilterXFunctionArgs *args, GError **error)
{
  // Checking filterx builtin functions first
  FilterXFunctionCtor ctor = filterx_builtin_function_ctor_lookup(function_name);

  if (!ctor)
    {
      // fallback to plugin lookup
      Plugin *p = cfg_find_plugin(cfg, LL_CONTEXT_FILTERX_FUNC, function_name);
      if (!p)
        return NULL;
      ctor = plugin_construct(p);
    }

  if (!ctor)
    return NULL;

  return ctor(args, error);
}

/* NOTE: takes the reference of "args_list" */
FilterXExpr *
filterx_function_lookup(GlobalConfig *cfg, const gchar *function_name, GList *args_list, GError **error)
{
  FilterXFunctionArgs *args = filterx_function_args_new(args_list, error);
  if (!args)
    return NULL;

  FilterXExpr *expr = _lookup_simple_function(cfg, function_name, args, error);
  if (expr)
    return expr;

  expr = _lookup_function(cfg, function_name, args, error);
  if (expr)
    return expr;

  if (!(*error))
    g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_FUNCTION_NOT_FOUND, "function not found");
  return NULL;
}

static FilterXExpr *
_lookup_generator_function(GlobalConfig *cfg, const gchar *function_name, FilterXFunctionArgs *args, GError **error)
{
  // Checking filterx builtin generator functions first
  FilterXFunctionCtor ctor = filterx_builtin_generator_function_ctor_lookup(function_name);

  if (!ctor)
    {
      // fallback to plugin lookup
      Plugin *p = cfg_find_plugin(cfg, LL_CONTEXT_FILTERX_GEN_FUNC, function_name);
      if (!p)
        return NULL;
      ctor = plugin_construct(p);
    }

  if (!ctor)
    return NULL;

  FilterXExpr *func = ctor(args, error);
  g_assert(!func || filterx_expr_is_generator(func));

  return func;
}

/* NOTE: takes the references of objects passed in "arguments" */
FilterXExpr *
filterx_generator_function_lookup(GlobalConfig *cfg, const gchar *function_name, GList *args_list, GError **error)
{
  FilterXFunctionArgs *args = filterx_function_args_new(args_list, error);
  if (!args)
    return NULL;

  FilterXExpr *expr = _lookup_generator_function(cfg, function_name, args, error);
  if (expr)
    return expr;

  if (!(*error))
    g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_FUNCTION_NOT_FOUND, "function not found");
  return NULL;
}

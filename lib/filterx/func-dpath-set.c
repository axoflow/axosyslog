/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 László Várady
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

#include "filterx/func-dpath-set.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-scope.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-variable.h"
#include "filterx/object-dict.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"

#define FILTERX_FUNC_DPATH_SET_USAGE "dpath_set(\"dict.path.to.create\", {...}, append=false/true)"

typedef struct FilterXDpathSet_
{
  FilterXFunction super;

  gchar **dpath;
  FilterXVariableHandle var;

  FilterXExpr *obj;

  gboolean append;
} FilterXDpathSet;

static FilterXObject *
_dpath_set_eval(FilterXExpr *s)
{
  FilterXDpathSet *self = (FilterXDpathSet *) s;

  FilterXObject *result = NULL;

  FilterXObject *dict = NULL;
  FilterXObject *obj = filterx_expr_eval(self->obj);
  if (!obj)
    goto exit;

  if (self->append && !filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(dict)))
    {
      filterx_eval_push_error("Second argument must be a dict when append mode is enabled. "
                              FILTERX_FUNC_DPATH_SET_USAGE, s, NULL);
      goto exit;
    }

  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXScope *scope = context->scope;
  FilterXVariable *variable = filterx_scope_lookup_variable(scope, self->var);

  if (!variable || !filterx_variable_is_set(variable))
    {
      variable = filterx_scope_register_variable(scope, FX_VAR_FLOATING, self->var);
      FilterXObject *d = filterx_dict_new();
      filterx_scope_set_variable(scope, variable, &d, TRUE);
      filterx_object_unref(d);
    }

  dict = filterx_variable_get_value(variable);

  if (!filterx_object_is_type_or_ref(dict, &FILTERX_TYPE_NAME(dict)))
    {
      filterx_eval_push_error("First argument has non-dict element in path. " FILTERX_FUNC_DPATH_SET_USAGE, s, NULL);
      goto exit;
    }

  if (*(self->dpath + 1) == NULL)
    {
      obj = filterx_object_cow_fork2(obj, NULL);
      if (self->append)
        filterx_dict_merge(dict, obj);
      else
        filterx_scope_set_variable(scope, variable, &obj, TRUE);

      result = filterx_boolean_new(TRUE);
      goto exit;
    }

  gchar **attr;
  for (attr = self->dpath + 1; *attr; attr++)
    {
      if (!self->append && *(attr + 1) == NULL)
        break;

      FILTERX_STRING_DECLARE_ON_STACK(key, *attr, -1);

      FilterXObject *value = filterx_object_getattr(dict, key);
      if (!value)
        {
          value = filterx_dict_new();
          filterx_object_cow_prepare(&value);
          filterx_object_setattr(dict, key, &value);
        }

      filterx_object_unref(dict);
      dict = value;

      filterx_object_unref(key);

      if (!filterx_object_is_type_or_ref(dict, &FILTERX_TYPE_NAME(dict)))
        {
          filterx_eval_push_error("First argument has non-dict element in path. " FILTERX_FUNC_DPATH_SET_USAGE,
                                  s, NULL);
          goto exit;
        }
    }

  obj = filterx_object_cow_fork2(obj, NULL);
  if (self->append)
    filterx_dict_merge(dict, obj);
  else
    {
      FILTERX_STRING_DECLARE_ON_STACK(key, *attr, -1);
      filterx_object_setattr(dict, key, &obj);
      filterx_object_unref(key);
    }

  result = filterx_boolean_new(TRUE);

exit:
  filterx_object_unref(obj);
  filterx_object_unref(dict);
  return result;
}

static FilterXExpr *
_dpath_set_optimize(FilterXExpr *s)
{
  FilterXDpathSet *self = (FilterXDpathSet *) s;

  self->obj = filterx_expr_optimize(self->obj);

  return filterx_function_optimize_method(&self->super);
}

static gboolean
_dpath_set_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXDpathSet *self = (FilterXDpathSet *) s;

  if (!filterx_expr_init(self->obj, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_dpath_set_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXDpathSet *self = (FilterXDpathSet *) s;

  filterx_expr_deinit(self->obj, cfg);

  filterx_function_deinit_method(&self->super, cfg);
}

static void
_dpath_set_free(FilterXExpr *s)
{
  FilterXDpathSet *self = (FilterXDpathSet *) s;

  g_strfreev(self->dpath);
  filterx_expr_unref(self->obj);

  filterx_function_free_method(&self->super);
}

static gboolean
_extract_append_arg(FilterXDpathSet *self, FilterXFunctionArgs *args, GError **error)
{
  gboolean exist, err;
  gboolean append = filterx_function_args_get_named_literal_boolean(args, "append", &exist, &err);

  if (exist && !err)
    self->append = append;

  if (err)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_UNEXPECTED_ARGS,
                  "append argument must be a literal boolean. " FILTERX_FUNC_DPATH_SET_USAGE);
      return FALSE;
    }

  return TRUE;
}

static inline gboolean
_is_valid_dpath_char(gchar c, gboolean first_char)
{
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
    return TRUE;

  if (!first_char && ((c >= '0' && c <= '9') || c == '.'))
    return TRUE;

  return FALSE;
}

static inline gboolean
_validate_dpath(const gchar *path)
{
  if (!*path)
    return FALSE;

  gboolean first_char = TRUE;
  const gchar *c;
  for (c = path; *c; c++)
    {
      if (!_is_valid_dpath_char(*c, first_char))
        return FALSE;

      if (*c == '.')
        first_char = TRUE;
      else
        first_char = FALSE;
    }

  if (*(c - 1) == '.')
    return FALSE;

  return TRUE;
}

static gboolean
_extract_args(FilterXDpathSet *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_DPATH_SET_USAGE);
      return FALSE;
    }

  const gchar *path = filterx_function_args_get_literal_string(args, 0, NULL);
  if (!path)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_UNEXPECTED_ARGS,
                  "first argument must be a literal string. " FILTERX_FUNC_DPATH_SET_USAGE);
      return FALSE;
    }

  if (!_validate_dpath(path))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_UNEXPECTED_ARGS,
                  "first argument must be a valid FilterX identifier. " FILTERX_FUNC_DPATH_SET_USAGE);
      return FALSE;
    }

  self->dpath = g_strsplit(path, ".", 0);
  self->var = filterx_map_varname_to_handle(*self->dpath, FX_VAR_FLOATING);

  self->obj = filterx_function_args_get_expr(args, 1);
  g_assert(self->obj);

  if (!_extract_append_arg(self, args, error))
    return FALSE;

  return TRUE;
}

FilterXExpr *
filterx_function_dpath_set_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXDpathSet *self = g_new0(FilterXDpathSet, 1);
  filterx_function_init_instance(&self->super, "dpath_set");

  self->super.super.eval = _dpath_set_eval;
  self->super.super.optimize = _dpath_set_optimize;
  self->super.super.init = _dpath_set_init;
  self->super.super.deinit = _dpath_set_deinit;
  self->super.super.free_fn = _dpath_set_free;

  if (!_extract_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

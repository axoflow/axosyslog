/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Szilard Parrag
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

#include "filterx/func-str.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-object.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "object-extractor.h"
#include "object-string.h"
#include "filterx/filterx-object.h"


#define FILTERX_FUNC_STARTSWITH_USAGE "Usage: startswith(my_string, my_prefix)"
#define FILTERX_FUNC_ENDSWITH_USAGE "Usage: endswith(my_string, my_prefix)"

static FilterXExpr *
_extract_haystack_arg(FilterXFunctionArgs *args, GError **error, const gchar *function_usage)
{
  gsize len = filterx_function_args_len(args);
  if (len != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. %s", function_usage);
      return NULL;
    }
  FilterXExpr *haystack = filterx_function_args_get_expr(args, 0);
  if (!haystack)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "haystack must be set. %s", function_usage);
      return NULL;
    }
  return haystack;
}

static gboolean
_extract_needle_arg(FilterXExprOrLiteral *needle, gboolean ignore_case, FilterXFunctionArgs *args, GError **error,
                    const gchar *function_usage)
{
  gsize len = filterx_function_args_len(args);
  if (len != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. %s", function_usage);
      return FALSE;
    }
  FilterXExpr *needle_expr = filterx_function_args_get_expr(args, 1);
  if (!filterx_expr_is_literal(needle_expr))
    {
      needle->expr = filterx_expr_ref(needle_expr);
      return TRUE;
    }

  gsize needle_str_len;
  const gchar *needle_str = filterx_function_args_get_literal_string(args, 1, &needle_str_len);
  if (!needle_str)
    return FALSE;

  if (ignore_case)
    {
      needle->literal.str = g_utf8_casefold(needle_str, needle_str_len);
      needle->literal.str_len = g_utf8_strlen(needle->literal.str, -1);
    }
  else
    {
      needle->literal.str = g_strdup(needle_str);
      needle->literal.str_len = needle_str_len;
    }
  return TRUE;
}

static gboolean
_extract_optional_args(gboolean *ignore_case, FilterXFunctionArgs *args, GError **error, const gchar *function_usage)
{
  gboolean exists, eval_error;
  gboolean value = filterx_function_args_get_named_literal_boolean(args, "ignorecase", &exists, &eval_error);
  if (!exists)
    return TRUE;

  if (eval_error)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "ignorecase argument must be boolean literal. %s", function_usage);
      return FALSE;
    }

  *ignore_case = value;
  return TRUE;
}

static gboolean
_eval_needle_expr(FilterXExpr *needle_expr, gboolean ignorecase, gchar **needle_str, gsize *needle_len)
{
  FilterXObject *needle_obj = filterx_expr_eval_typed(needle_expr);
  if (!needle_obj)
    {
      filterx_eval_push_error_info("failed to evaluate needle", needle_expr,
                                   g_strdup_printf("invalid expression"), TRUE);
      return FALSE;
    }
  const gchar *needle_str_res;
  if (!filterx_object_extract_string(needle_obj, &needle_str_res, needle_len))
    {
      filterx_eval_push_error_info("failed to extract needle, it must be a string", needle_expr,
                                   g_strdup_printf("got %s instead", needle_obj->type->name), TRUE);
      filterx_object_unref(needle_obj);
      return FALSE;
    }
  if (!ignorecase)
    *needle_str = g_strdup(needle_str_res);
  else
    {
      *needle_str = g_utf8_casefold(needle_str_res, *needle_len);
      *needle_len = g_utf8_strlen(*needle_str, -1);
    }
  filterx_object_unref(needle_obj);
  return TRUE;
}

static gboolean
_eval_haystack_expr(FilterXExpr *haystack, gboolean ignorecase, gchar **haystack_str, gsize *haystack_len)
{

  FilterXObject *haystack_obj = filterx_expr_eval(haystack);
  if (!haystack_obj)
    {
      filterx_eval_push_error_info("failed to evaluate haystack", haystack,
                                   g_strdup_printf("invalid expression"), TRUE);
      return FALSE;
    }
  const gchar *haystack_str_res;
  if (!filterx_object_extract_string(haystack_obj, &haystack_str_res, haystack_len))
    {
      filterx_eval_push_error_info("failed to extract haystack, it must be a string", haystack,
                                   g_strdup_printf("got %s instead", haystack_obj->type->name), TRUE);
      filterx_object_unref(haystack_obj);
      return FALSE;
    }
  if (!ignorecase)
    {
      *haystack_str = g_strdup(haystack_str_res);
    }
  else
    {
      *haystack_str = g_utf8_casefold(haystack_str_res, *haystack_len);
      *haystack_len = g_utf8_strlen(*haystack_str, -1);
    }
  filterx_object_unref(haystack_obj);
  return TRUE;
}


FilterXObject *
_filterx_function_startswith_eval(FilterXExpr *s)
{
  FilterXFuncStartsWith *self = (FilterXFuncStartsWith *) s;

  gsize haystack_len;
  gchar *haystack_str;
  if(!_eval_haystack_expr(self->haystack, self->ignore_case, &haystack_str, &haystack_len))
    return NULL;
  gsize needle_len;
  gchar *needle_str;
  if(!self->needle.expr)
    {
      needle_str = self->needle.literal.str;
      needle_len = self->needle.literal.str_len;
    }
  else if (!_eval_needle_expr(self->needle.expr, self->ignore_case, &needle_str, &needle_len))
    return NULL;
  gboolean startswith = FALSE;
  if (needle_len > haystack_len)
    goto exit;

  if (memcmp(haystack_str, needle_str, needle_len) == 0)
    startswith = TRUE;

exit:
  g_free(haystack_str);
  if(self->needle.expr)
    g_free(needle_str);
  return filterx_boolean_new(startswith);
}


static void
_filterx_function_startswith_free(FilterXExpr *s)
{
  FilterXFuncStartsWith *self = (FilterXFuncStartsWith *) s;

  filterx_expr_unref(self->haystack);
  if (self->needle.expr)
    filterx_expr_unref(self->needle.expr);

  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_function_startswith_new(const gchar *function_name, FilterXFunctionArgs *args, GError **error)
{
  FilterXFuncStartsWith *self = g_new0(FilterXFuncStartsWith, 1);
  filterx_function_init_instance(&self->super, function_name);
  self->ignore_case = FALSE;
  if(!_extract_optional_args(&self->ignore_case, args, error, FILTERX_FUNC_STARTSWITH_USAGE))
    goto error;

  self->haystack = _extract_haystack_arg(args, error, FILTERX_FUNC_STARTSWITH_USAGE);
  if (!self->haystack)
    goto error;
  if (!(_extract_needle_arg(&self->needle, self->ignore_case, args, error, FILTERX_FUNC_STARTSWITH_USAGE))
      || !filterx_function_args_check(args, error))
    goto error;

  self->super.super.eval = _filterx_function_startswith_eval;
  self->super.super.free_fn = _filterx_function_startswith_free;
  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}


FilterXObject *
_filterx_function_endswith_eval(FilterXExpr *s)
{
  FilterXFuncEndsWith *self = (FilterXFuncEndsWith *) s;

  gsize haystack_len;
  gchar *haystack_str;
  if(!_eval_haystack_expr(self->haystack, self->ignore_case, &haystack_str, &haystack_len))
    return NULL;
  gsize needle_len;
  gchar *needle_str;
  if(!self->needle.expr)
    {
      needle_str = self->needle.literal.str;
      needle_len = self->needle.literal.str_len;
    }
  else if (!_eval_needle_expr(self->needle.expr, self->ignore_case, &needle_str, &needle_len))
    return NULL;
  gboolean endswith = FALSE;
  if (needle_len > haystack_len)
    goto exit;

  if (memcmp(haystack_str + haystack_len - needle_len, needle_str, needle_len) == 0)
    endswith = TRUE;

exit:
  g_free(haystack_str);
  if(self->needle.expr)
    g_free(needle_str);
  return filterx_boolean_new(endswith);
}


static void
_filterx_function_endswith_free(FilterXExpr *s)
{
  FilterXFuncEndsWith *self = (FilterXFuncEndsWith *) s;

  filterx_expr_unref(self->haystack);
  if (self->needle.expr)
    filterx_expr_unref(self->needle.expr);

  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_function_endswith_new(const gchar *function_name, FilterXFunctionArgs *args, GError **error)
{
  FilterXFuncEndsWith *self = g_new0(FilterXFuncEndsWith, 1);
  filterx_function_init_instance(&self->super, function_name);
  self->ignore_case = FALSE;
  if(!_extract_optional_args(&self->ignore_case, args, error, FILTERX_FUNC_STARTSWITH_USAGE))
    goto error;

  self->haystack = _extract_haystack_arg(args, error, FILTERX_FUNC_ENDSWITH_USAGE);
  if (!self->haystack)
    goto error;
  if (!(_extract_needle_arg(&self->needle, self->ignore_case, args, error, FILTERX_FUNC_ENDSWITH_USAGE))
      || !filterx_function_args_check(args, error))
    goto error;

  self->super.super.eval = _filterx_function_endswith_eval;
  self->super.super.free_fn = _filterx_function_endswith_free;
  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

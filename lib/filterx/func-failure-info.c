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

#include "filterx/func-failure-info.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-error.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-list-interface.h"

#include "scratch-buffers.h"

#define FILTERX_FUNC_FAILURE_INFO_ENABLE_USAGE "Usage: failure_info_enable(collect_falsy=false/true);"
#define FILTERX_FUNC_FAILURE_INFO_META_USAGE "Usage: failure_info_meta({});"

typedef struct FilterXFunctionFailureInfoEnable_
{
  FilterXFunction super;
  gboolean collect_falsy;
} FilterXFunctionFailureInfoEnable;

typedef struct FilterXFunctionFailureInfoMeta_
{
  FilterXFunction super;
  FilterXObject *metadata;
} FilterXFunctionFailureInfoMeta;

static inline void
_set_subscript_cstr(FilterXObject *dict, const gchar *key, const gchar *value)
{
  FILTERX_STRING_DECLARE_ON_STACK(fx_key, key, -1);
  FILTERX_STRING_DECLARE_ON_STACK(fx_value, value, -1);

  filterx_object_set_subscript(dict, fx_key, &fx_value);

  filterx_object_unref(fx_value);
  filterx_object_unref(fx_key);
}

static FilterXObject *
_create_dict_from_failure_info_entry(FilterXFailureInfo *fi)
{
  FilterXObject *fx_finfo_entry = filterx_dict_new();
  filterx_object_cow_prepare(&fx_finfo_entry);

  if (fi->meta)
    {
      FILTERX_STRING_DECLARE_ON_STACK(meta_key, "meta", -1);
      filterx_object_set_subscript(fx_finfo_entry, meta_key, &fi->meta);
      filterx_object_unref(meta_key);
    }

  _set_subscript_cstr(fx_finfo_entry, "location", filterx_expr_format_location(fi->error.expr)->str);
  _set_subscript_cstr(fx_finfo_entry, "line", filterx_expr_get_text(fi->error.expr));
  _set_subscript_cstr(fx_finfo_entry, "error", filterx_error_format(&fi->error));

  return fx_finfo_entry;
}

static FilterXObject *
_failure_info_eval(FilterXExpr *s)
{
  GArray *failure_info = filterx_eval_get_failure_info(filterx_eval_get_context());
  FilterXObject *fx_finfo = filterx_list_new();

  if (!failure_info)
    return fx_finfo;

  ScratchBuffersMarker mark;
  scratch_buffers_mark(&mark);

  for (guint i = 0; i < failure_info->len; ++i)
    {
      FilterXFailureInfo *failure_info_entry = &g_array_index(failure_info, FilterXFailureInfo, i);

      FilterXObject *finfo_entry = _create_dict_from_failure_info_entry(failure_info_entry);
      filterx_list_set_subscript(fx_finfo, i, &finfo_entry);
      filterx_object_unref(finfo_entry);
    }

  scratch_buffers_reclaim_marked(mark);

  return fx_finfo;
}

static FilterXObject *
_failure_info_clear_eval(FilterXExpr *s)
{
  filterx_eval_clear_failure_info(filterx_eval_get_context());

  return filterx_boolean_new(TRUE);
}

static FilterXObject *
_failure_info_meta_eval(FilterXExpr *s)
{
  FilterXFunctionFailureInfoMeta *self = (FilterXFunctionFailureInfoMeta *) s;

  FilterXEvalContext *context = filterx_eval_get_context();
  if (!filterx_eval_get_failure_info(context))
    goto exit;

  filterx_eval_set_current_frame_meta(context, self->metadata);

exit:
  return filterx_boolean_new(TRUE);
}

static FilterXObject *
_failure_info_enable_eval(FilterXExpr *s)
{
  FilterXFunctionFailureInfoEnable *self = (FilterXFunctionFailureInfoEnable *) s;

  filterx_eval_enable_failure_info(filterx_eval_get_context(), self->collect_falsy);

  return filterx_boolean_new(TRUE);
}

static gboolean
_extract_failure_info_enable_args(FilterXFunctionFailureInfoEnable *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FAILURE_INFO_ENABLE_USAGE);
      return FALSE;
    }

  gboolean exists, eval_error;
  gboolean collect_falsy = filterx_function_args_get_named_literal_boolean(args, "collect_falsy", &exists, &eval_error);

  if (eval_error)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "collect_falsy argument must be boolean literal. %s", FILTERX_FUNC_FAILURE_INFO_ENABLE_USAGE);
      return FALSE;
    }

  if (!exists)
    return TRUE;

  self->collect_falsy = collect_falsy;
  return TRUE;
}

static gboolean
_extract_failure_info_meta_args(FilterXFunctionFailureInfoMeta *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FAILURE_INFO_META_USAGE);
      return FALSE;
    }

  FilterXExpr *meta_expr = filterx_function_args_get_expr(args, 0);
  if (!filterx_expr_is_literal(meta_expr) && !filterx_expr_is_literal_container(meta_expr))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be literal. %s", FILTERX_FUNC_FAILURE_INFO_META_USAGE);
      filterx_expr_unref(meta_expr);
      return FALSE;
    }

  FilterXObject *metadata = filterx_expr_eval(meta_expr);
  self->metadata = metadata;

  filterx_expr_unref(meta_expr);
  return TRUE;
}

static gboolean
_check_zero_args(FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "requires 0 arguments");
      return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_fn_failure_info_enable_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFailureInfoEnable *self = g_new0(FilterXFunctionFailureInfoEnable, 1);
  filterx_function_init_instance(&self->super, "failure_info_enable");
  self->super.super.eval = _failure_info_enable_eval;

  if (!_extract_failure_info_enable_args(self, args, error) || !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FilterXExpr *
filterx_fn_failure_info_clear_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunction *self = g_new0(FilterXFunction, 1);
  filterx_function_init_instance(self, "failure_info_clear");
  self->super.eval = _failure_info_clear_eval;

  if (!_check_zero_args(args, error) || !filterx_function_args_check(args, error))
    {
      filterx_function_args_free(args);
      filterx_expr_unref(&self->super);
      return NULL;
    }

  filterx_function_args_free(args);
  return &self->super;
}

FilterXExpr *
filterx_fn_failure_info_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunction *self = g_new0(FilterXFunction, 1);
  filterx_function_init_instance(self, "failure_info");
  self->super.eval = _failure_info_eval;

  if (!_check_zero_args(args, error) || !filterx_function_args_check(args, error))
    {
      filterx_function_args_free(args);
      filterx_expr_unref(&self->super);
      return NULL;
    }

  filterx_function_args_free(args);
  return &self->super;
}

static void
_failure_info_meta_free(FilterXExpr *s)
{
  FilterXFunctionFailureInfoMeta *self = (FilterXFunctionFailureInfoMeta *) s;

  filterx_object_unref(self->metadata);
  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_fn_failure_info_meta_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFailureInfoMeta *self = g_new0(FilterXFunctionFailureInfoMeta, 1);
  filterx_function_init_instance(&self->super, "failure_info_meta");
  self->super.super.eval = _failure_info_meta_eval;
  self->super.super.free_fn = _failure_info_meta_free;

  if (!_extract_failure_info_meta_args(self, args, error) || !filterx_function_args_check(args, error))
    {
      filterx_function_args_free(args);
      filterx_expr_unref(&self->super.super);
      return NULL;
    }

  filterx_function_args_free(args);
  return &self->super.super;
}

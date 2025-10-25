/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx-func-format-kv.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/filterx-eval.h"

#include "scratch-buffers.h"
#include "utf8utils.h"

#define FILTERX_FUNC_FORMAT_KV_USAGE "Usage: format_kv(kvs_dict, value_separator=\"=\", pair_separator=\", \")"

typedef struct FilterXFunctionFormatKV_
{
  FilterXFunction super;
  FilterXExpr *kvs;
  gchar value_separator;
  gchar *pair_separator;
} FilterXFunctionFormatKV;

static gboolean
_append_kv_to_buffer(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FilterXFunctionFormatKV *self = ((gpointer *) user_data)[0];
  GString *buffer = ((gpointer *) user_data)[1];

  FilterXObject *value_unwrapped = filterx_ref_unwrap_ro(value);
  if (filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(dict)) ||
      filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(list)))
    {
      msg_debug("FilterX: format_kv(): skipping object, type not supported",
                evt_tag_str("type", filterx_object_get_type_name(value)));
      return TRUE;
    }

  if (buffer->len)
    g_string_append(buffer, self->pair_separator);

  if (!filterx_object_str_append(key, buffer))
    {
      filterx_eval_push_error_static_info("Failed to evaluate format_kv()", &self->super.super,
                                          "str_append() method failed on key");
      return FALSE;
    }

  g_string_append_c(buffer, self->value_separator);

  gsize len_before_value = buffer->len;
  if (!filterx_object_str_append(value, buffer))
    {
      filterx_eval_push_error_static_info("Failed to evaluate format_kv()", &self->super.super,
                                          "str_append() method failed on value");
      return FALSE;
    }

  /* TODO: make the characters here configurable. */
  if (memchr(buffer->str + len_before_value, ' ', buffer->len - len_before_value) != NULL)
    {
      ScratchBuffersMarker marker;
      GString *value_buffer = scratch_buffers_alloc_and_mark(&marker);

      g_string_assign(value_buffer, buffer->str + len_before_value);
      g_string_truncate(buffer, len_before_value);
      g_string_append_c(buffer, '"');
      append_unsafe_utf8_as_escaped_binary(buffer, value_buffer->str, value_buffer->len, AUTF8_UNSAFE_QUOTE);
      g_string_append_c(buffer, '"');

      scratch_buffers_reclaim_marked(marker);
    }

  return TRUE;
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionFormatKV *self = (FilterXFunctionFormatKV *) s;

  FilterXObject *obj = filterx_expr_eval_typed(self->kvs);
  if (!obj)
    {
      filterx_eval_push_error_static_info("Failed to evaluate format_kv()", &self->super.super,
                                          "Failed to evaluate kvs_dict. " FILTERX_FUNC_FORMAT_KV_USAGE);
      return NULL;
    }

  FilterXObject *kvs = filterx_ref_unwrap_ro(obj);
  if (!filterx_object_is_type(kvs, &FILTERX_TYPE_NAME(dict)))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate format_kv()", &self->super.super,
                                          "Object must be a dict, got: %s. " FILTERX_FUNC_FORMAT_KV_USAGE,
                                          filterx_object_get_type_name(obj));
      filterx_object_unref(obj);
      return NULL;
    }

  GString *formatted = scratch_buffers_alloc();
  gpointer user_data[] = { self, formatted };
  gboolean success = filterx_dict_iter(kvs, _append_kv_to_buffer, user_data);

  filterx_object_unref(obj);
  return success ? filterx_string_new(formatted->str, formatted->len) : NULL;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionFormatKV *self = (FilterXFunctionFormatKV *) s;

  self->kvs = filterx_expr_optimize(self->kvs);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatKV *self = (FilterXFunctionFormatKV *) s;

  if (!filterx_expr_init(self->kvs, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatKV *self = (FilterXFunctionFormatKV *) s;

  filterx_expr_deinit(self->kvs, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionFormatKV *self = (FilterXFunctionFormatKV *) s;

  filterx_expr_unref(self->kvs);
  g_free(self->pair_separator);
  filterx_function_free_method(&self->super);
}

static gboolean
_extract_value_separator_arg(FilterXFunctionFormatKV *self, FilterXFunctionArgs *args, GError **error)
{
  gboolean exists;
  gsize value_separator_len;
  const gchar *value_separator = filterx_function_args_get_named_literal_string(args, "value_separator",
                                 &value_separator_len, &exists);
  if (!exists)
    return TRUE;

  if (!value_separator)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "value_separator must be a string literal. " FILTERX_FUNC_FORMAT_KV_USAGE);
      return FALSE;
    }

  if (value_separator_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "value_separator must be a single character. " FILTERX_FUNC_FORMAT_KV_USAGE);
      return FALSE;
    }

  self->value_separator = value_separator[0];
  return TRUE;
}

static gboolean
_extract_pair_separator_arg(FilterXFunctionFormatKV *self, FilterXFunctionArgs *args, GError **error)
{
  gboolean exists;
  gsize pair_separator_len;
  const gchar *pair_separator = filterx_function_args_get_named_literal_string(args, "pair_separator",
                                &pair_separator_len, &exists);
  if (!exists)
    return TRUE;

  if (!pair_separator)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "pair_separator must be a string literal. " FILTERX_FUNC_FORMAT_KV_USAGE);
      return FALSE;
    }

  if (pair_separator_len == 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "pair_separator must be non-zero length. " FILTERX_FUNC_FORMAT_KV_USAGE);
      return FALSE;
    }

  g_free(self->pair_separator);
  self->pair_separator = g_strdup(pair_separator);
  return TRUE;
}

static gboolean
_extract_arguments(FilterXFunctionFormatKV *self, FilterXFunctionArgs *args, GError **error)
{
  gsize args_len = filterx_function_args_len(args);
  if (args_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FORMAT_KV_USAGE);
      return FALSE;
    }

  self->kvs = filterx_function_args_get_expr(args, 0);
  if (!self->kvs)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "kvs_dict must be set. " FILTERX_FUNC_FORMAT_KV_USAGE);
      return FALSE;
    }

  if (!_extract_value_separator_arg(self, args, error))
    return FALSE;

  if (!_extract_pair_separator_arg(self, args, error))
    return FALSE;

  return TRUE;
}

static gboolean
_format_kv_walk(FilterXExpr *s, FilterXExprWalkOrder order, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionFormatKV *self = (FilterXFunctionFormatKV *) s;

  FilterXExpr *exprs[] = { self->kvs, NULL };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_walk(exprs[i], order, f, user_data))
        return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_function_format_kv_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFormatKV *self = g_new0(FilterXFunctionFormatKV, 1);
  filterx_function_init_instance(&self->super, "format_kv");

  self->super.super.eval = _eval;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.walk_children = _format_kv_walk;
  self->super.super.free_fn = _free;
  self->value_separator = '=';
  self->pair_separator = g_strdup(", ");

  if (!_extract_arguments(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(format_kv, filterx_function_format_kv_new);

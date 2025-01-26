/*
 * Copyright (c) 2024 shifter
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "filterx-func-format-csv.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object-istype.h"
#include "filterx/filterx-ref.h"

#include "scratch-buffers.h"
#include "utf8utils.h"

#define FILTERX_FUNC_FORMAT_CSV_USAGE "Usage: format_csv({list or dict}, [" \
  FILTERX_FUNC_FORMAT_CSV_ARG_NAME_COLUMNS"={list}," \
  FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DELIMITER"={string literal}," \
  FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DEFAULT_VALUE"={string literal}])"

typedef struct FilterXFunctionFormatCSV_
{
  FilterXFunction super;
  FilterXExpr *input;
  gchar delimiter;
  FilterXExpr *columns;
  FilterXObject *default_value;
} FilterXFunctionFormatCSV;

static gboolean
_append_to_buffer(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FilterXFunctionFormatCSV *self = ((gpointer *) user_data)[0];
  GString *buffer = ((gpointer *) user_data)[1];

  if (!value)
    value = self->default_value;

  FilterXObject *value_unwrapped = filterx_ref_unwrap_ro(value);
  if (filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(dict)) ||
      filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(list)))
    {
      msg_debug("FilterX: format_csv(): skipping object, type not supported",
                evt_tag_str("type", value_unwrapped->type->name));
      return TRUE;
    }

  if (buffer->len)
    g_string_append_c(buffer, self->delimiter);

  gsize len_before_value = buffer->len;
  if (!filterx_object_repr_append(value, buffer))
    return FALSE;

  /* TODO: make the characters here configurable. */
  if (memchr(buffer->str + len_before_value, self->delimiter, buffer->len - len_before_value) != NULL)
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

static gboolean
_handle_list_input(FilterXFunctionFormatCSV *self, FilterXObject *csv_data, GString *formatted)
{
  guint64 size;
  if (!filterx_object_len(csv_data, &size))
    return FALSE;

  gpointer user_data[] = { self, formatted };
  gboolean success = TRUE;
  for (guint64 i = 0; i < size && success; i++)
    {
      FilterXObject *elt = filterx_list_get_subscript(csv_data, i);
      success = _append_to_buffer(NULL, elt, user_data);
      filterx_object_unref(elt);
    }
  return success;
}

static gboolean
_handle_dict_input(FilterXFunctionFormatCSV *self, FilterXObject *csv_data, GString *formatted)
{
  gpointer user_data[] = { self, formatted };
  guint64 size;
  if (self->columns)
    {
      FilterXObject *cols = filterx_expr_eval(self->columns);
      FilterXObject *cols_unwrapped = filterx_ref_unwrap_ro(cols);
      if (!cols || !filterx_object_is_type(cols_unwrapped, &FILTERX_TYPE_NAME(list)) || !filterx_object_len(cols, &size))
        {
          filterx_object_unref(cols);
          filterx_eval_push_error("Columns must represented as list. " FILTERX_FUNC_FORMAT_CSV_USAGE, &self->super.super, NULL);
          return FALSE;
        }

      gboolean success = TRUE;
      for (guint64 i = 0; i < size && success; i++)
        {
          FilterXObject *col = filterx_list_get_subscript(cols_unwrapped, i);
          FilterXObject *elt = filterx_object_get_subscript(csv_data, col);
          success = _append_to_buffer(col, elt, user_data);
          filterx_object_unref(col);
          filterx_object_unref(elt);
        }
      filterx_object_unref(cols);
      return success;
    }
  else
    return filterx_dict_iter(csv_data, _append_to_buffer, user_data);
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionFormatCSV *self = (FilterXFunctionFormatCSV *) s;

  FilterXObject *csv_data = filterx_expr_eval_typed(self->input);
  if (!csv_data)
    return NULL;

  gboolean success = FALSE;
  GString *formatted = scratch_buffers_alloc();

  FilterXObject *csv_data_unwrapped = filterx_ref_unwrap_ro(csv_data);
  if (filterx_object_is_type(csv_data_unwrapped, &FILTERX_TYPE_NAME(list)))
    success = _handle_list_input(self, csv_data_unwrapped, formatted);
  else if (filterx_object_is_type(csv_data_unwrapped, &FILTERX_TYPE_NAME(dict)))
    success = _handle_dict_input(self, csv_data_unwrapped, formatted);
  else
    filterx_eval_push_error("input must be a dict or list. " FILTERX_FUNC_FORMAT_CSV_USAGE, s, csv_data);

  filterx_object_unref(csv_data);
  return success ? filterx_string_new(formatted->str, formatted->len) : NULL;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionFormatCSV *self = (FilterXFunctionFormatCSV *) s;

  self->input = filterx_expr_optimize(self->input);
  self->columns = filterx_expr_optimize(self->columns);

  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatCSV *self = (FilterXFunctionFormatCSV *) s;

  if (!filterx_expr_init(self->input, cfg))
    return FALSE;

  if (!filterx_expr_init(self->columns, cfg))
    {
      filterx_expr_deinit(self->input, cfg);
      return FALSE;
    }

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatCSV *self = (FilterXFunctionFormatCSV *) s;

  filterx_expr_deinit(self->input, cfg);
  filterx_expr_deinit(self->columns, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionFormatCSV *self = (FilterXFunctionFormatCSV *) s;

  filterx_object_unref(self->default_value);
  filterx_expr_unref(self->input);
  filterx_expr_unref(self->columns);
  filterx_function_free_method(&self->super);
}

static FilterXExpr *
_extract_columns_expr(FilterXFunctionArgs *args, GError **error)
{
  return filterx_function_args_get_named_expr(args, FILTERX_FUNC_FORMAT_CSV_ARG_NAME_COLUMNS);
}

static gboolean
_extract_delimiter_arg(FilterXFunctionFormatCSV *self, FilterXFunctionArgs *args, GError **error)
{
  gboolean exists;
  gsize delimiter_len;
  const gchar *delimiter = filterx_function_args_get_named_literal_string(args,
                           FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DELIMITER,
                           &delimiter_len, &exists);
  if (!exists)
    return TRUE;

  if (!delimiter)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "delimiter must be a string literal. " FILTERX_FUNC_FORMAT_CSV_USAGE);
      return FALSE;
    }

  if (delimiter_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "delimiter must be a single character. " FILTERX_FUNC_FORMAT_CSV_USAGE);
      return FALSE;
    }

  self->delimiter = delimiter[0];
  return TRUE;
}

static gboolean
_extract_default_value_arg(FilterXFunctionFormatCSV *self, FilterXFunctionArgs *args, GError **error)
{
  gboolean exists;
  FilterXObject *default_value = filterx_function_args_get_named_literal_object(args,
                                 FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DEFAULT_VALUE, &exists);

  if (!exists)
    return TRUE;

  if (!default_value || !filterx_object_is_type(default_value, &FILTERX_TYPE_NAME(string)))
    {
      filterx_object_unref(default_value);
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "default_value must be a string literal. " FILTERX_FUNC_FORMAT_CSV_USAGE);
      return FALSE;
    }

  filterx_object_unref(self->default_value);
  self->default_value = default_value;

  return TRUE;
}

static gboolean
_extract_arguments(FilterXFunctionFormatCSV *self, FilterXFunctionArgs *args, GError **error)
{
  gsize args_len = filterx_function_args_len(args);
  if (args_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FORMAT_CSV_USAGE);
      return FALSE;
    }

  self->input = filterx_function_args_get_expr(args, 0);
  if (!self->input)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "input must be set. " FILTERX_FUNC_FORMAT_CSV_USAGE);
      return FALSE;
    }

  if (!_extract_delimiter_arg(self, args, error))
    return FALSE;

  if (!_extract_default_value_arg(self, args, error))
    return FALSE;

  self->columns = _extract_columns_expr(args, error);

  return TRUE;
}

FilterXExpr *
filterx_function_format_csv_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFormatCSV *self = g_new0(FilterXFunctionFormatCSV, 1);
  filterx_function_init_instance(&self->super, "format_csv");

  self->super.super.eval = _eval;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.free_fn = _free;
  self->delimiter = ',';
  self->default_value = filterx_string_new("", -1);

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

FILTERX_FUNCTION(format_csv, filterx_function_format_csv_new);

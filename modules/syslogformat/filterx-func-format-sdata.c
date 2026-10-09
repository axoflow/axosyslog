/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx-func-format-sdata.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "logmsg/logmsg.h"
#include "scratch-buffers.h"

#define FILTERX_FUNC_FORMAT_SDATA_USAGE "Usage: format_sdata(sdata_dict)"

typedef void (*SDataAppendEscapedFunc)(GString *result, const gchar *sstr, gssize len);

typedef struct FilterXFunctionFormatSData_
{
  FilterXFunction super;
  FilterXExpr *sdata_expr;
} FilterXFunctionFormatSData;

static gboolean
_append_escaped(GString *buffer, FilterXObject *obj, SDataAppendEscapedFunc append_escaped)
{
  const gchar *str;
  gsize len;
  if (filterx_object_extract_string_ref(obj, &str, &len))
    {
      append_escaped(buffer, str, len);
      return TRUE;
    }

  GString *str_buffer = scratch_buffers_alloc();
  if (!filterx_object_str(obj, str_buffer))
    {
      filterx_eval_push_error("Failed to evaluate format_sdata(): cannot convert object to string", obj);
      return FALSE;
    }

  append_escaped(buffer, str_buffer->str, str_buffer->len);
  return TRUE;
}

static gboolean
_append_param(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  GString *buffer = (GString *) user_data;

  g_string_append_c(buffer, ' ');
  if (!_append_escaped(buffer, key, log_msg_sdata_append_key_escaped))
    return FALSE;

  g_string_append(buffer, "=\"");
  if (!_append_escaped(buffer, value, log_msg_sdata_append_escaped))
    return FALSE;

  g_string_append_c(buffer, '"');
  return TRUE;
}

static gboolean
_append_element(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  GString *buffer = (GString *) user_data;

  FilterXObject *params = filterx_ref_unwrap_ro(value);
  if (!filterx_object_is_type(params, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate format_sdata()",
                                          "SD-ID value must be a dict, got: %s. " FILTERX_FUNC_FORMAT_SDATA_USAGE,
                                          filterx_object_get_type_name(value));
      return FALSE;
    }

  g_string_append_c(buffer, '[');
  if (!_append_escaped(buffer, key, log_msg_sdata_append_key_escaped))
    return FALSE;

  if (!filterx_object_iter(params, _append_param, buffer))
    return FALSE;

  g_string_append_c(buffer, ']');
  return TRUE;
}

gboolean
filterx_format_sdata_append(GString *buffer, FilterXObject *sdata)
{
  FilterXObject *elements = filterx_ref_unwrap_ro(sdata);
  if (!filterx_object_is_type(elements, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate format_sdata()",
                                          "Object must be a dict, got: %s. " FILTERX_FUNC_FORMAT_SDATA_USAGE,
                                          filterx_object_get_type_name(sdata));
      return FALSE;
    }

  gsize len_before = buffer->len;
  if (!filterx_object_iter(elements, _append_element, buffer))
    return FALSE;

  if (buffer->len == len_before)
    g_string_append_c(buffer, '-');

  return TRUE;
}

static FilterXObject *
_format_sdata_eval(FilterXExpr *s)
{
  FilterXFunctionFormatSData *self = (FilterXFunctionFormatSData *) s;

  FilterXObject *sdata = filterx_expr_eval_typed(self->sdata_expr);
  if (!sdata)
    return NULL;

  GString *buffer = scratch_buffers_alloc();
  gboolean success = filterx_format_sdata_append(buffer, sdata);

  filterx_object_unref(sdata);
  return success ? filterx_string_new(buffer->str, buffer->len) : NULL;
}

static void
_format_sdata_free(FilterXExpr *s)
{
  FilterXFunctionFormatSData *self = (FilterXFunctionFormatSData *) s;

  filterx_expr_unref(self->sdata_expr);
  filterx_function_free_method(&self->super);
}

static gboolean
_format_sdata_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionFormatSData *self = (FilterXFunctionFormatSData *) s;

  return filterx_expr_visit(s, &self->sdata_expr, f, user_data);
}

static gboolean
_format_sdata_extract_arguments(FilterXFunctionFormatSData *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FORMAT_SDATA_USAGE);
      return FALSE;
    }

  self->sdata_expr = filterx_function_args_get_expr(args, 0);
  return TRUE;
}

FilterXExpr *
filterx_function_format_sdata_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFormatSData *self = g_new0(FilterXFunctionFormatSData, 1);
  filterx_function_init_instance(&self->super, "format_sdata", FXE_READ);

  self->super.super.eval = _format_sdata_eval;
  self->super.super.walk_children = _format_sdata_walk;
  self->super.super.free_fn = _format_sdata_free;

  if (!_format_sdata_extract_arguments(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(format_sdata, filterx_function_format_sdata_new);

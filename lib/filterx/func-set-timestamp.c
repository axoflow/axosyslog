/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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

#include "func-set-timestamp.h"
#include "object-datetime.h"
#include "filterx/object-extractor.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"

#define FILTERX_FUNC_SET_TIMESTAMP_USAGE "Usage: set_timestamp(datetime, stamp=[\"stamp\", \"recvd\"])"

typedef struct FilterXFunctionSetTimestamp_
{
  FilterXFunction super;

  FilterXExpr *datetime_expr;
  LogMessageTimeStamp timestamp_idx;
} FilterXFunctionSetTimestamp;

static FilterXObject *
_set_timestamp_eval(FilterXExpr *s)
{
  return NULL;
}

static void
_set_timestamp_free(FilterXExpr *s)
{
  FilterXFunctionSetTimestamp *self = (FilterXFunctionSetTimestamp *) s;

  filterx_expr_unref(self->datetime_expr);
  filterx_function_free_method(&self->super);
}

static const gchar *
_extract_set_timestamp_idx_str(FilterXFunctionArgs *args, GError **error)
{
  gboolean exists;
  const gchar *idx_str = filterx_function_args_get_named_literal_string(args, "stamp", NULL, &exists);

  if (!exists)
    {
      return "stamp";
    }

  return idx_str;
}

static FilterXExpr *
_extract_set_timestamp_datetime_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *datetime_expr = filterx_function_args_get_expr(args, 0);
  if (!datetime_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: datetime. " FILTERX_FUNC_SET_TIMESTAMP_USAGE);
      return NULL;
    }

  return datetime_expr;
}

static gboolean
_extract_set_timestamp_args(FilterXFunctionSetTimestamp *self, FilterXFunctionArgs *args, GError **error)
{
  gsize len = filterx_function_args_len(args);

  if (len != 1 && len != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_SET_TIMESTAMP_USAGE);
      return FALSE;
    }

  self->datetime_expr = _extract_set_timestamp_datetime_expr(args, error);
  if (!self->datetime_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "First argument must be of datetime type. " FILTERX_FUNC_SET_TIMESTAMP_USAGE);
      return FALSE;
    }

  const gchar *idx_str = _extract_set_timestamp_idx_str(args, error);
  if (!idx_str)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Second argument must be string type. " FILTERX_FUNC_SET_TIMESTAMP_USAGE);
      return FALSE;
    }

  self->timestamp_idx = log_msg_lookup_time_stamp_name(idx_str);

  if ((self->timestamp_idx != LM_TS_STAMP) && (self->timestamp_idx != LM_TS_RECVD))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Invalid timestamp type " FILTERX_FUNC_SET_TIMESTAMP_USAGE);
      return FALSE;
    }

  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_set_timestamp_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionSetTimestamp *self = g_new0(FilterXFunctionSetTimestamp, 1);
  filterx_function_init_instance(&self->super, "set_timestamp");

  self->super.super.eval = _set_timestamp_eval;
  self->super.super.free_fn = _set_timestamp_free;

  if (!_extract_set_timestamp_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

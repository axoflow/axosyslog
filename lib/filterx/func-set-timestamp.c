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
  FilterXFunctionSetTimestamp *self = (FilterXFunctionSetTimestamp *) s;

  FilterXObject *datetime_obj = filterx_expr_eval(self->datetime_expr);
  if (!datetime_obj)
    {
      filterx_eval_push_error("Failed to evaluate second argument. " FILTERX_FUNC_SET_TIMESTAMP_USAGE, s, NULL);
      return NULL;
    }

  UnixTime datetime = UNIX_TIME_INIT;

  if (!filterx_object_extract_datetime(datetime_obj, &datetime))
    {
      filterx_object_unref(datetime_obj);
      filterx_eval_push_error("Second argument must be of datetime type. " FILTERX_FUNC_SET_TIMESTAMP_USAGE, s, NULL);
      return NULL;
    }

  filterx_object_unref(datetime_obj);

  FilterXEvalContext *context = filterx_eval_get_context();
  LogMessage *msg = context->msg;

  msg->timestamps[self->timestamp_idx] = datetime;

  return filterx_boolean_new(TRUE);
}

static FilterXExpr *
_set_timestamp_optimize(FilterXExpr *s)
{
  FilterXFunctionSetTimestamp *self = (FilterXFunctionSetTimestamp *) s;

  self->datetime_expr = filterx_expr_optimize(self->datetime_expr);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_set_timestamp_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionSetTimestamp *self = (FilterXFunctionSetTimestamp *) s;

  if (!filterx_expr_init(self->datetime_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_set_timestamp_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionSetTimestamp *self = (FilterXFunctionSetTimestamp *) s;

  filterx_expr_deinit(self->datetime_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
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
  self->super.super.optimize = _set_timestamp_optimize;
  self->super.super.init = _set_timestamp_init;
  self->super.super.deinit = _set_timestamp_deinit;
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

#define FILTERX_FUNC_GET_TIMESTAMP_USAGE "Usage: get_timestamp(datetime, stamp=[\"stamp\", \"recvd\"])"

typedef struct FilterXFunctionGetTimestamp_
{
  FilterXFunction super;

  LogMessageTimeStamp timestamp_idx;
} FilterXFunctionGetTimestamp;


static FilterXObject *
_get_timestamp_eval(FilterXExpr *s)
{
  FilterXFunctionGetTimestamp *self = (FilterXFunctionGetTimestamp *) s;
  FilterXEvalContext *context = filterx_eval_get_context();
  LogMessage *msg = context->msg;

  return filterx_datetime_new(&msg->timestamps[self->timestamp_idx]);
}

static const gchar *
_extract_get_timestamp_idx_str(FilterXFunctionArgs *args, GError **error)
{
  gboolean exists;
  const gchar *idx_str = filterx_function_args_get_named_literal_string(args, "stamp", NULL, &exists);

  if (!exists)
    {
      return "stamp";
    }

  return idx_str;
}

static gboolean
_extract_get_timestamp_args(FilterXFunctionGetTimestamp *self, FilterXFunctionArgs *args, GError **error)
{
  gsize len = filterx_function_args_len(args);

  if (len > 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_GET_TIMESTAMP_USAGE);
      return FALSE;
    }

  const gchar *idx_str = _extract_get_timestamp_idx_str(args, error);
  if (!idx_str)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Second argument must be string type. " FILTERX_FUNC_GET_TIMESTAMP_USAGE);
      return FALSE;
    }

  self->timestamp_idx = log_msg_lookup_time_stamp_name(idx_str);

  if ((self->timestamp_idx != LM_TS_STAMP) && (self->timestamp_idx != LM_TS_RECVD))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Invalid timestamp type " FILTERX_FUNC_GET_TIMESTAMP_USAGE);
      return FALSE;
    }

  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_get_timestamp_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionGetTimestamp *self = g_new0(FilterXFunctionGetTimestamp, 1);
  filterx_function_init_instance(&self->super, "get_timestamp");

  self->super.super.eval = _get_timestamp_eval;

  if (!_extract_get_timestamp_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

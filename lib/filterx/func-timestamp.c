/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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

#include "func-timestamp.h"
#include "object-datetime.h"
#include "filterx/object-extractor.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "timeutils/cache.h"

#define FILTERX_FUNC_SET_TIMESTAMP_USAGE "Usage: set_timestamp(datetime, stamp=[\"stamp\", \"recvd\"])"
#define FILTERX_FUNC_FIX_TIMEZONE_USAGE "Usage: fix_timezone(datetime, timezone_str)"
#define FILTERX_FUNC_GUESS_TIMEZONE_USAGE "Usage: guess_timezone(datetime)"
#define FILTERX_FUNC_SET_TIMEZONE_USAGE "Usage: set_timezone(datetime, timezone_str)"

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
      filterx_eval_push_error("Failed to evaluate first argument. " FILTERX_FUNC_SET_TIMESTAMP_USAGE, s, NULL);
      return NULL;
    }

  UnixTime datetime = UNIX_TIME_INIT;

  if (!filterx_object_extract_datetime(datetime_obj, &datetime))
    {
      filterx_object_unref(datetime_obj);
      filterx_eval_push_error("First argument must be of datetime type. " FILTERX_FUNC_SET_TIMESTAMP_USAGE, s, NULL);
      return NULL;
    }

  filterx_object_unref(datetime_obj);

  FilterXEvalContext *context = filterx_eval_get_context();
  LogMessage *msg = context->msg;

  msg->timestamps[self->timestamp_idx] = datetime;

  return filterx_boolean_new(TRUE);
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

  if (len != 1)
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

  gint timestamp_idx = log_msg_lookup_time_stamp_name(idx_str);

  if (timestamp_idx < 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Invalid timestamp type " FILTERX_FUNC_SET_TIMESTAMP_USAGE);
      return FALSE;
    }

  self->timestamp_idx = timestamp_idx;

  return TRUE;
}

static gboolean
_set_timestamp_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionSetTimestamp *self = (FilterXFunctionSetTimestamp *) s;

  FilterXExpr **exprs[] = { &self->datetime_expr };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }
  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_set_timestamp_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionSetTimestamp *self = g_new0(FilterXFunctionSetTimestamp, 1);

  filterx_function_init_instance(&self->super, "set_timestamp", FXE_WORLD);

  self->super.super.eval = _set_timestamp_eval;
  self->super.super.walk_children = _set_timestamp_walk;
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

#define FILTERX_FUNC_GET_TIMESTAMP_USAGE "Usage: get_timestamp(stamp=[\"stamp\", \"recvd\"])"

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

  gint timestamp_idx = log_msg_lookup_time_stamp_name(idx_str);

  if (timestamp_idx < 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Invalid timestamp type " FILTERX_FUNC_GET_TIMESTAMP_USAGE);
      return FALSE;
    }

  self->timestamp_idx = timestamp_idx;

  return TRUE;
}

static gboolean
_get_timestamp_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  /* no child expressions */
  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_get_timestamp_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionGetTimestamp *self = g_new0(FilterXFunctionGetTimestamp, 1);
  filterx_function_init_instance(&self->super, "get_timestamp", FXE_READ);

  self->super.super.eval = _get_timestamp_eval;
  self->super.super.walk_children = _get_timestamp_walk;

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

typedef struct FilterXFunctionFixTimezone_
{
  FilterXFunction super;
  gchar *timezone;
  FilterXExpr *datetime_expr;
} FilterXFunctionFixTimezone;

static FilterXObject *
_fix_timezone_eval(FilterXExpr *s)
{
  FilterXFunctionFixTimezone *self = (FilterXFunctionFixTimezone *) s;

  FilterXObject *datetime_obj = filterx_expr_eval(self->datetime_expr);
  if (!datetime_obj)
    {
      filterx_eval_push_error("Failed to evaluate first argument. " FILTERX_FUNC_FIX_TIMEZONE_USAGE, s, NULL);
      return NULL;
    }

  UnixTime datetime = UNIX_TIME_INIT;

  if (!filterx_object_extract_datetime(datetime_obj, &datetime))
    {
      filterx_object_unref(datetime_obj);
      filterx_eval_push_error("First argument must be of datetime type. " FILTERX_FUNC_FIX_TIMEZONE_USAGE, s, NULL);
      return NULL;
    }

  TimeZoneInfo *tzinfo = cached_get_time_zone_info(self->timezone);

  if (!tzinfo)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate fix_timezone", &self->super.super,
                                          "Unknown timezone: %s", self->timezone);
      filterx_object_unref(datetime_obj);
      return NULL;
    }

  unix_time_fix_timezone_with_tzinfo(&datetime, tzinfo);

  filterx_object_unref(datetime_obj);

  return filterx_datetime_new(&datetime);
}

static gboolean
_fix_timezone_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionFixTimezone *self = (FilterXFunctionFixTimezone *) s;

  FilterXExpr **exprs[] = { &self->datetime_expr };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

static void
_fix_timezone_free(FilterXExpr *s)
{
  FilterXFunctionFixTimezone *self = (FilterXFunctionFixTimezone *) s;

  g_free(self->timezone);
  filterx_expr_unref(self->datetime_expr);
  filterx_function_free_method(&self->super);
}

static const gchar *
_extract_fix_timezone_timezone(FilterXFunctionArgs *args, GError **error)
{
  const gchar *timezone_str = filterx_function_args_get_literal_string(args, 1, NULL);
  if (!timezone_str)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: timezone_str. " FILTERX_FUNC_FIX_TIMEZONE_USAGE);
      return NULL;
    }

  return timezone_str;
}

static FilterXExpr *
_extract_fix_timezone_datetime_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *datetime_expr = filterx_function_args_get_expr(args, 0);
  if (!datetime_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: datetime. " FILTERX_FUNC_FIX_TIMEZONE_USAGE);
      return NULL;
    }

  return datetime_expr;
}

static gboolean
_extract_fix_timezone_args(FilterXFunctionFixTimezone *self, FilterXFunctionArgs *args, GError **error)
{
  gsize len = filterx_function_args_len(args);

  if (len != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FIX_TIMEZONE_USAGE);
      return FALSE;
    }

  self->datetime_expr = _extract_fix_timezone_datetime_expr(args, error);
  if (!self->datetime_expr)
    return FALSE;

  self->timezone = g_strdup(_extract_fix_timezone_timezone(args, error));
  if (!self->timezone)
    return FALSE;

  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_fix_timezone_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFixTimezone *self = g_new0(FilterXFunctionFixTimezone, 1);

  filterx_function_init_instance(&self->super, "fix_timezone", FXE_READ);

  self->super.super.eval = _fix_timezone_eval;
  self->super.super.walk_children = _fix_timezone_walk;
  self->super.super.free_fn = _fix_timezone_free;

  if (!_extract_fix_timezone_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

typedef struct FilterXFunctionGuessTimezone_
{
  FilterXFunction super;
  FilterXExpr *datetime_expr;
} FilterXFunctionGuessTimezone;

static FilterXObject *
_guess_timezone_eval(FilterXExpr *s)
{
  FilterXFunctionGuessTimezone *self = (FilterXFunctionGuessTimezone *) s;

  FilterXObject *datetime_obj = filterx_expr_eval(self->datetime_expr);
  if (!datetime_obj)
    {
      filterx_eval_push_error("Failed to evaluate first argument. " FILTERX_FUNC_GUESS_TIMEZONE_USAGE, s, NULL);
      return NULL;
    }

  UnixTime datetime = UNIX_TIME_INIT;

  if (!filterx_object_extract_datetime(datetime_obj, &datetime))
    {
      filterx_object_unref(datetime_obj);
      filterx_eval_push_error("First argument must be of datetime type. " FILTERX_FUNC_GUESS_TIMEZONE_USAGE, s, NULL);
      return NULL;
    }

  unix_time_fix_timezone_assuming_the_time_matches_real_time(&datetime);

  filterx_object_unref(datetime_obj);

  return filterx_datetime_new(&datetime);
}

static gboolean
_guess_timezone_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionGuessTimezone *self = (FilterXFunctionGuessTimezone *) s;

  FilterXExpr **exprs[] = { &self->datetime_expr };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

static void
_guess_timezone_free(FilterXExpr *s)
{
  FilterXFunctionGuessTimezone *self = (FilterXFunctionGuessTimezone *) s;

  filterx_expr_unref(self->datetime_expr);
  filterx_function_free_method(&self->super);
}

static FilterXExpr *
_extract_guess_timezone_datetime_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *datetime_expr = filterx_function_args_get_expr(args, 0);
  if (!datetime_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: datetime. " FILTERX_FUNC_GUESS_TIMEZONE_USAGE);
      return NULL;
    }

  return datetime_expr;
}

static gboolean
_extract_guess_timezone_arg(FilterXFunctionGuessTimezone *self, FilterXFunctionArgs *args, GError **error)
{
  gsize len = filterx_function_args_len(args);

  if (len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_GUESS_TIMEZONE_USAGE);
      return FALSE;
    }

  self->datetime_expr = _extract_guess_timezone_datetime_expr(args, error);
  if (!self->datetime_expr)
    return FALSE;

  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_guess_timezone_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionGuessTimezone *self = g_new0(FilterXFunctionGuessTimezone, 1);

  filterx_function_init_instance(&self->super, "guess_timezone", FXE_READ);

  self->super.super.eval = _guess_timezone_eval;
  self->super.super.walk_children = _guess_timezone_walk;
  self->super.super.free_fn = _guess_timezone_free;

  if (!_extract_guess_timezone_arg(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

typedef struct FilterXFunctionSetTimezone_
{
  FilterXFunction super;
  gchar *timezone;
  FilterXExpr *datetime_expr;
} FilterXFunctionSetTimezone;

static FilterXObject *
_set_timezone_eval(FilterXExpr *s)
{
  FilterXFunctionSetTimezone *self = (FilterXFunctionSetTimezone *) s;

  FilterXObject *datetime_obj = filterx_expr_eval(self->datetime_expr);
  if (!datetime_obj)
    {
      filterx_eval_push_error("Failed to evaluate first argument. " FILTERX_FUNC_SET_TIMEZONE_USAGE, s, NULL);
      return NULL;
    }

  UnixTime datetime = UNIX_TIME_INIT;

  if (!filterx_object_extract_datetime(datetime_obj, &datetime))
    {
      filterx_object_unref(datetime_obj);
      filterx_eval_push_error("First argument must be of datetime type. " FILTERX_FUNC_SET_TIMEZONE_USAGE, s, NULL);
      return NULL;
    }

  TimeZoneInfo *tzinfo = cached_get_time_zone_info(self->timezone);

  if (!tzinfo)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate set_timezone", &self->super.super,
                                          "Unknown timezone: %s", self->timezone);
      filterx_object_unref(datetime_obj);
      return NULL;
    }

  unix_time_set_timezone_with_tzinfo(&datetime, tzinfo);

  filterx_object_unref(datetime_obj);

  return filterx_datetime_new(&datetime);
}

static gboolean
_set_timezone_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionSetTimezone *self = (FilterXFunctionSetTimezone *) s;

  FilterXExpr **exprs[] = { &self->datetime_expr };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

static void
_set_timezone_free(FilterXExpr *s)
{
  FilterXFunctionSetTimezone *self = (FilterXFunctionSetTimezone *) s;

  g_free(self->timezone);
  filterx_expr_unref(self->datetime_expr);
  filterx_function_free_method(&self->super);
}

static const gchar *
_extract_set_timezone_timezone(FilterXFunctionArgs *args, GError **error)
{
  const gchar *timezone_str = filterx_function_args_get_literal_string(args, 1, NULL);
  if (!timezone_str)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: timezone_str. " FILTERX_FUNC_SET_TIMEZONE_USAGE);
      return NULL;
    }

  return timezone_str;
}

static FilterXExpr *
_extract_set_timezone_datetime_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *datetime_expr = filterx_function_args_get_expr(args, 0);
  if (!datetime_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: datetime. " FILTERX_FUNC_SET_TIMEZONE_USAGE);
      return NULL;
    }

  return datetime_expr;
}

static gboolean
_extract_set_timezone_args(FilterXFunctionSetTimezone *self, FilterXFunctionArgs *args, GError **error)
{
  gsize len = filterx_function_args_len(args);

  if (len != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_SET_TIMEZONE_USAGE);
      return FALSE;
    }

  self->datetime_expr = _extract_set_timezone_datetime_expr(args, error);
  if (!self->datetime_expr)
    return FALSE;

  self->timezone = g_strdup(_extract_set_timezone_timezone(args, error));
  if (!self->timezone)
    return FALSE;

  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_set_timezone_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionSetTimezone *self = g_new0(FilterXFunctionSetTimezone, 1);

  filterx_function_init_instance(&self->super, "set_timezone", FXE_READ);

  self->super.super.eval = _set_timezone_eval;
  self->super.super.walk_children = _set_timezone_walk;
  self->super.super.free_fn = _set_timezone_free;

  if (!_extract_set_timezone_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

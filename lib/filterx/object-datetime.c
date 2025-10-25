/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Tamás Kosztyu <tamas.kosztyu@axoflow.com>
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "object-datetime.h"
#include "scratch-buffers.h"
#include "str-format.h"
#include "str-utils.h"
#include "timeutils/wallclocktime.h"
#include "timeutils/conv.h"
#include "timeutils/misc.h"
#include "timeutils/format.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-message-value.h"
#include "filterx/object-null.h"
#include "filterx/expr-literal.h"
#include "generic-number.h"
#include "filterx/filterx-eval.h"
#include "filterx-globals.h"
#include "compat/json.h"

#define FILTERX_FUNC_STRPTIME_USAGE "Usage: strptime(time_str, format_str_1, ..., format_str_N)"
#define FILTERX_FUNC_STRFTIME_USAGE "Usage: strftime(format_str, datetime)"

#define FILTERX_DATETIME_CACHE_SIZE 1
FilterXObject *fx_datetime_cache[FILTERX_DATETIME_CACHE_SIZE];

typedef struct _FilterXDateTime
{
  FilterXObject super;
  UnixTime ut;
} FilterXDateTime;

static gboolean
_truthy(FilterXObject *s)
{
  FilterXDateTime *self = (FilterXDateTime *) s;
  return self->ut.ut_sec != 0 || self->ut.ut_usec != 0;
}

/* FIXME: delegate formatting and parsing to UnixTime */
static void
_convert_unix_time_to_string(const UnixTime *ut, GString *result, gboolean include_tzofs)
{
  format_int64_padded(result, -1, ' ', 10, ut->ut_sec);
  g_string_append_c(result, '.');
  format_uint64_padded(result, 6, '0', 10, ut->ut_usec);
  if (include_tzofs && ut->ut_gmtoff != -1)
    {
      if (ut->ut_gmtoff >= 0)
        g_string_append_c(result, '+');

      format_int64_padded(result, 2, '0', 10, ut->ut_gmtoff / 3600);
      g_string_append_c(result, ':');
      format_int64_padded(result, 2, '0', 10, (ut->ut_gmtoff % 3600) / 60);
    }
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXDateTime *self = (FilterXDateTime *) s;

  *t = LM_VT_DATETIME;
  _convert_unix_time_to_string(&self->ut, repr, TRUE);
  return TRUE;
}

FilterXObject *
_datetime_new(const UnixTime *ut)
{
  FilterXDateTime *self = g_new0(FilterXDateTime, 1);

  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(datetime));
  self->ut = *ut;
  return &self->super;
}

FilterXObject *
filterx_datetime_new(const UnixTime *ut)
{
  if (ut->ut_sec == 0 && ut->ut_usec == 0)
    return fx_datetime_cache[0];
  return _datetime_new(ut);
}

/* NOTE: Consider using filterx_object_extract_datetime() to also support message_value. */
UnixTime
filterx_datetime_get_value(FilterXObject *s)
{
  FilterXDateTime *self = (FilterXDateTime *) s;

  return self->ut;
}


FilterXObject *
filterx_typecast_datetime(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(datetime)))
    {
      filterx_object_ref(object);
      return object;
    }

  gint64 i;
  if (filterx_object_extract_integer(object, &i))
    {
      UnixTime ut = unix_time_from_unix_epoch_usec((guint64) MAX(i, 0));
      return filterx_datetime_new(&ut);
    }

  gdouble d;
  if (filterx_object_extract_double(object, &d))
    {
      UnixTime ut = unix_time_from_unix_epoch_usec((gint64)(d * USEC_PER_SEC));
      return filterx_datetime_new(&ut);
    }

  return filterx_typecast_datetime_isodate(s, args, args_len);
}

FilterXObject *
filterx_typecast_datetime_isodate(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  const gchar *str;
  gsize len;
  if (!filterx_object_extract_string_ref(object, &str, &len))
    return NULL;

  UnixTime ut = UNIX_TIME_INIT;
  WallClockTime wct = WALL_CLOCK_TIME_INIT;

  if (len == 0)
    {
      filterx_eval_push_error_static_info("Failed to cast string to datetime", s, "Argument is an empty string");
      return NULL;
    }

  gchar *end = wall_clock_time_strptime(&wct, datefmt_isodate, str);
  if (end && *end != 0)
    {
      filterx_eval_push_error_info_printf("Failed to cast string to datetime", s,
                                          "Unable to parse ISO 8601 format. str: '%s', end: '%s'",
                                          str, end);
      return NULL;
    }

  convert_wall_clock_time_to_unix_time(&wct, &ut);
  return filterx_datetime_new(&ut);
}

gboolean
datetime_repr(const UnixTime *ut, GString *repr)
{
  WallClockTime wct = WALL_CLOCK_TIME_INIT;
  convert_unix_time_to_wall_clock_time(ut, &wct);
  append_format_wall_clock_time(&wct, repr, TS_FMT_ISO, 3);
  return TRUE;
}

gboolean
datetime_format_json(const UnixTime *ut, GString *repr)
{
  g_string_append_c(repr, '"');
  _convert_unix_time_to_string(ut, repr, FALSE);
  g_string_append_c(repr, '"');
  return TRUE;
}

gboolean
datetime_str(const UnixTime *ut, GString *repr)
{
  _convert_unix_time_to_string(ut, repr, FALSE);
  return TRUE;
}

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  FilterXDateTime *self = (FilterXDateTime *) s;

  return datetime_repr(&self->ut, repr);
}

static gboolean
_str(FilterXObject *s, GString *repr)
{
  FilterXDateTime *self = (FilterXDateTime *) s;

  return datetime_str(&self->ut, repr);
}

static gboolean
_format_json(FilterXObject *s, GString *repr)
{
  FilterXDateTime *self = (FilterXDateTime *) s;

  return datetime_format_json(&self->ut, repr);
}

static FilterXObject *
_add(FilterXObject *s, FilterXObject *object)
{
  FilterXDateTime *self = (FilterXDateTime *) s;

  UnixTime result;

  gint64 i;
  if (filterx_object_extract_integer(object, &i))
    {
      result = unix_time_add_duration(self->ut, i);
      goto success;
    }

  gdouble d;
  if (filterx_object_extract_double(object, &d))
    {
      result = unix_time_add_duration(self->ut, (gint64)(d * USEC_PER_SEC));
      goto success;
    }

  return NULL;

success:
  return filterx_datetime_new(&result);
}


typedef struct FilterXFunctionStrptime_
{
  FilterXFunction super;
  FilterXExpr *time_str_expr;
  GPtrArray *formats;
} FilterXFunctionStrptime;

static FilterXExpr *
_strptime_optimize(FilterXExpr *s)
{
  FilterXFunctionStrptime *self = (FilterXFunctionStrptime *) s;

  self->time_str_expr = filterx_expr_optimize(self->time_str_expr);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_strptime_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionStrptime *self = (FilterXFunctionStrptime *) s;

  if (!filterx_expr_init(self->time_str_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_strptime_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionStrptime *self = (FilterXFunctionStrptime *) s;

  filterx_expr_deinit(self->time_str_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static FilterXObject *
_strptime_eval(FilterXExpr *s)
{
  FilterXFunctionStrptime *self = (FilterXFunctionStrptime *) s;

  FilterXObject *result = NULL;

  FilterXObject *time_str_obj = filterx_expr_eval(self->time_str_expr);
  if (!time_str_obj)
    {
      filterx_eval_push_error("Failed to evaluate first argument. " FILTERX_FUNC_STRPTIME_USAGE, s, NULL);
      return NULL;
    }

  const gchar *time_str;
  gsize time_str_len;
  gboolean extract_success = filterx_object_extract_string_ref(time_str_obj, &time_str, &time_str_len);

  if (!extract_success)
    {
      filterx_object_unref(time_str_obj);
      filterx_eval_push_error("First argument must be string typed. " FILTERX_FUNC_STRPTIME_USAGE, s, NULL);
      return NULL;
    }

  WallClockTime wct = WALL_CLOCK_TIME_INIT;
  UnixTime ut = UNIX_TIME_INIT;
  gchar *end = NULL;

  for (guint i = 0; i < self->formats->len; i++)
    {
      const gchar *format = g_ptr_array_index(self->formats, i);
      end = wall_clock_time_strptime(&wct, format, time_str);
      if (!end)
        {
          msg_debug("filterx: unable to parse time",
                    evt_tag_str("time_string", time_str),
                    evt_tag_str("format", format));
        }
      else
        break;
    }

  wall_clock_time_guess_missing_fields(&wct);

  if (end)
    {
      convert_wall_clock_time_to_unix_time(&wct, &ut);
      result = filterx_datetime_new(&ut);
    }
  else
    result = filterx_null_new();

  filterx_object_unref(time_str_obj);
  return result;
}

static void
_strptime_free(FilterXExpr *s)
{
  FilterXFunctionStrptime *self = (FilterXFunctionStrptime *) s;

  filterx_expr_unref(self->time_str_expr);
  if (self->formats)
    g_ptr_array_free(self->formats, TRUE);
  filterx_function_free_method(&self->super);
}

static GPtrArray *
_extract_strptime_formats(FilterXFunctionArgs *args, GError **error)
{
  guint64 num_formats = filterx_function_args_len(args) - 1;
  GPtrArray *formats = g_ptr_array_new_full(num_formats, g_free);

  for (guint64 i = 0; i < num_formats; i++)
    {
      const gchar *format = filterx_function_args_get_literal_string(args, i+1, NULL);
      if (!format)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      "format_str_%" G_GUINT64_FORMAT " argument must be string literal. " FILTERX_FUNC_STRPTIME_USAGE,
                      i+1);
          goto error;
        }

      g_ptr_array_add(formats, g_strdup(format));
    }

  return formats;

error:
  g_ptr_array_free(formats, TRUE);
  return NULL;
}

static FilterXExpr *
_extract_time_str_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *time_str_expr = filterx_function_args_get_expr(args, 0);
  if (!time_str_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: time_str. " FILTERX_FUNC_STRPTIME_USAGE);
      return NULL;
    }

  return time_str_expr;
}

static gboolean
_extract_strptime_args(FilterXFunctionStrptime *self, FilterXFunctionArgs *args, GError **error)
{
  gsize len = filterx_function_args_len(args);
  if (len < 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_STRPTIME_USAGE);
      return FALSE;
    }

  self->time_str_expr = _extract_time_str_expr(args, error);
  if (!self->time_str_expr)
    return FALSE;

  self->formats = _extract_strptime_formats(args, error);
  if (!self->formats)
    return FALSE;

  return TRUE;
}

static gboolean
_strptime_walk(FilterXExpr *s, FilterXExprWalkOrder order, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionStrptime *self = (FilterXFunctionStrptime *) s;

  FilterXExpr *exprs[] = { self->time_str_expr, NULL };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_walk(exprs[i], order, f, user_data))
        return FALSE;
    }

  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_strptime_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionStrptime *self = g_new0(FilterXFunctionStrptime, 1);
  filterx_function_init_instance(&self->super, "strptime");
  self->super.super.init = _strptime_init;
  self->super.super.deinit = _strptime_deinit;
  self->super.super.optimize = _strptime_optimize;
  self->super.super.eval = _strptime_eval;
  self->super.super.walk_children = _strptime_walk;
  self->super.super.free_fn = _strptime_free;

  if (!_extract_strptime_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

typedef struct FilterXFunctionStrftime_
{
  FilterXFunction super;
  gchar *format;
  FilterXExpr *datetime_expr;
} FilterXFunctionStrftime;

static FilterXObject *
_strftime_eval(FilterXExpr *s)
{
  FilterXFunctionStrftime *self = (FilterXFunctionStrftime *) s;

  FilterXObject *datetime_obj = filterx_expr_eval(self->datetime_expr);
  if (!datetime_obj)
    {
      filterx_eval_push_error("Failed to evaluate second argument. " FILTERX_FUNC_STRFTIME_USAGE, s, NULL);
      return NULL;
    }

  UnixTime datetime = UNIX_TIME_INIT;
  WallClockTime wct = WALL_CLOCK_TIME_INIT;

  if (!filterx_object_extract_datetime(datetime_obj, &datetime))
    {
      filterx_object_unref(datetime_obj);
      filterx_eval_push_error("Second argument must be of datetime type. " FILTERX_FUNC_STRFTIME_USAGE, s, NULL);
      return NULL;
    }

  filterx_object_unref(datetime_obj);

  convert_unix_time_to_wall_clock_time (&datetime, &wct);

  const gsize MAX_RESULT_STR_LEN = 256;
  gchar result_str[MAX_RESULT_STR_LEN];

  size_t date_size = wall_clock_time_strftime(&wct, result_str, MAX_RESULT_STR_LEN, self->format);

  if (!date_size)
    return filterx_null_new();

  return filterx_string_new(result_str, date_size);
}

static FilterXExpr *
_strftime_optimize(FilterXExpr *s)
{
  FilterXFunctionStrftime *self = (FilterXFunctionStrftime *) s;

  self->datetime_expr = filterx_expr_optimize(self->datetime_expr);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_strftime_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionStrftime *self = (FilterXFunctionStrftime *) s;

  if (!filterx_expr_init(self->datetime_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_strftime_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionStrftime *self = (FilterXFunctionStrftime *) s;

  filterx_expr_deinit(self->datetime_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_strftime_free(FilterXExpr *s)
{
  FilterXFunctionStrftime *self = (FilterXFunctionStrftime *) s;

  g_free(self->format);
  filterx_expr_unref(self->datetime_expr);
  filterx_function_free_method(&self->super);
}

static const gchar *
_extract_strftime_format(FilterXFunctionArgs *args, GError **error)
{
  const gchar *format = filterx_function_args_get_literal_string(args, 0, NULL);
  if (!format)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: format_str. " FILTERX_FUNC_STRFTIME_USAGE);
      return NULL;
    }

  return format;
}

static FilterXExpr *
_extract_strftime_datetime_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *datetime_expr = filterx_function_args_get_expr(args, 1);
  if (!datetime_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: time_str. " FILTERX_FUNC_STRFTIME_USAGE);
      return NULL;
    }

  return datetime_expr;
}

static gboolean
_extract_strftime_args(FilterXFunctionStrftime *self, FilterXFunctionArgs *args, GError **error)
{
  gsize len = filterx_function_args_len(args);

  if (len != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_STRFTIME_USAGE);
      return FALSE;
    }

  self->format = g_strdup(_extract_strftime_format(args, error));
  if (!self->format)
    {
      return FALSE;
    }

  self->datetime_expr = _extract_strftime_datetime_expr(args, error);
  if (!self->datetime_expr)
    {
      return FALSE;
    }

  return TRUE;
}

static gboolean
_strftime_walk(FilterXExpr *s, FilterXExprWalkOrder order, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionStrftime *self = (FilterXFunctionStrftime *) s;

  FilterXExpr *exprs[] = { self->datetime_expr, NULL };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_walk(exprs[i], order, f, user_data))
        return FALSE;
    }

  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_strftime_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionStrftime *self = g_new0(FilterXFunctionStrftime, 1);
  filterx_function_init_instance(&self->super, "strftime");

  self->super.super.eval = _strftime_eval;
  self->super.super.optimize = _strftime_optimize;
  self->super.super.init = _strftime_init;
  self->super.super.deinit = _strftime_deinit;
  self->super.super.walk_children = _strftime_walk;
  self->super.super.free_fn = _strftime_free;

  if (!_extract_strftime_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    {
      goto error;
    }

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_DEFINE_TYPE(datetime, FILTERX_TYPE_NAME(object),
                    .truthy = _truthy,
                    .format_json = _format_json,
                    .marshal = _marshal,
                    .repr = _repr,
                    .str = _str,
                    .add = _add,
                   );

void
filterx_datetime_global_init(void)
{
  UnixTime zero = {0};

  fx_datetime_cache[0] = _datetime_new(&zero);
  filterx_object_hibernate(fx_datetime_cache[0]);
}

void
filterx_datetime_global_deinit(void)
{
  for (gint i = 0; i < FILTERX_DATETIME_CACHE_SIZE; i++)
    {
      filterx_object_unhibernate_and_free(fx_datetime_cache[i]);
    }
}

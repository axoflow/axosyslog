/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx-func-format-syslog.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "hostname.h"
#include "timeutils/unixtime.h"
#include "timeutils/wallclocktime.h"
#include "timeutils/conv.h"
#include "timeutils/format.h"
#include "str-format.h"
#include "scratch-buffers.h"

#define FILTERX_FUNC_FORMAT_SYSLOG_5424_USAGE "Usage: format_syslog_5424(message, add_octet_count=false, pri=expr, " \
                                              "timestamp=expr, host=expr, program=expr, pid=expr, msgid=expr)"

typedef struct FilterXFunctionFormatSyslog5424_
{
  FilterXFunction super;
  gboolean add_octet_count;
  FilterXExpr *pri_expr;
  FilterXExpr *timestamp_expr;
  FilterXExpr *host_expr;
  FilterXExpr *program_expr;
  FilterXExpr *pid_expr;
  FilterXExpr *msgid_expr;
  FilterXExpr *message_expr;
} FilterXFunctionFormatSyslog5424;

static inline FilterXObject *
_eval_optional_or_fallible_expr(FilterXExpr *expr)
{
  if (!expr)
    return NULL;

  FilterXObject *obj = filterx_expr_eval(expr);
  if (!obj)
    filterx_eval_clear_errors();

  return obj;
}

static inline const gchar *
_cast_to_string(FilterXObject *obj, gsize *len)
{
  if (!obj)
    {
      *len = 0;
      return "";
    }

  const gchar *str;
  if (filterx_object_extract_string_ref(obj, &str, len))
    return str;

  GString *buf = scratch_buffers_alloc();
  if (filterx_object_str(obj, buf))
    {
      *len = buf->len;
      return buf->str;
    }

  filterx_eval_push_error("Cannot convert object to string", NULL, obj);
  return NULL;
}

static inline gint64
_cast_to_int(FilterXObject *obj)
{
  if (!obj)
    return -1;

  gint64 value;
  if (filterx_object_extract_integer(obj, &value))
    return value;

  return -1;
}

static inline FilterXObject *
_get_message(FilterXFunctionFormatSyslog5424 *self, const gchar **message, gsize *message_len)
{
  FilterXObject *message_obj = filterx_expr_eval(self->message_expr);
  if (!message_obj)
    return NULL;
  *message = _cast_to_string(message_obj, message_len);
  return message_obj;
}

static inline FilterXObject *
_get_pri(FilterXFunctionFormatSyslog5424 *self, LogMessage *logmsg, gint64 *pri,
         const gchar **pri_str, gsize *pri_str_len)
{
  FilterXObject *pri_obj = _eval_optional_or_fallible_expr(self->pri_expr);

  *pri = _cast_to_int(pri_obj);
  if (*pri != -1)
    return pri_obj;

  if (pri_obj && filterx_object_extract_string_ref(pri_obj, pri_str, pri_str_len))
    return pri_obj;

  *pri = logmsg->pri;
  return pri_obj;
}

static inline FilterXObject *
_get_timestamp(FilterXFunctionFormatSyslog5424 *self, LogMessage *logmsg, WallClockTime *timestamp)
{
  UnixTime ut;
  FilterXObject *timestamp_obj = _eval_optional_or_fallible_expr(self->timestamp_expr);
  if (!timestamp_obj || !filterx_object_extract_datetime(timestamp_obj, &ut))
    ut = logmsg->timestamps[LM_TS_STAMP];
  convert_unix_time_to_wall_clock_time(&ut, timestamp);
  return timestamp_obj;
}

static inline FilterXObject *
_get_host(FilterXFunctionFormatSyslog5424 *self, const gchar **host, gsize *host_len)
{
  FilterXObject *host_obj = _eval_optional_or_fallible_expr(self->host_expr);
  *host = _cast_to_string(host_obj, host_len);
  if (*host_len == 0)
    {
      *host = "-";
      *host_len = 1;
    }
  return host_obj;
}

static inline FilterXObject *
_get_program(FilterXFunctionFormatSyslog5424 *self, const gchar **program, gsize *program_len)
{
  FilterXObject *program_obj = _eval_optional_or_fallible_expr(self->program_expr);
  *program = _cast_to_string(program_obj, program_len);
  if (*program_len == 0)
    {
      *program = "-";
      *program_len = 1;
    }
  return program_obj;
}

static inline FilterXObject *
_get_pid(FilterXFunctionFormatSyslog5424 *self, const gchar **pid, gsize *pid_len)
{
  FilterXObject *pid_obj = _eval_optional_or_fallible_expr(self->pid_expr);
  *pid = _cast_to_string(pid_obj, pid_len);
  if (*pid_len == 0)
    {
      *pid = "-";
      *pid_len = 1;
    }
  return pid_obj;
}

static inline FilterXObject *
_get_msgid(FilterXFunctionFormatSyslog5424 *self, const gchar **msgid, gsize *msgid_len)
{
  FilterXObject *msgid_obj = _eval_optional_or_fallible_expr(self->msgid_expr);
  *msgid = _cast_to_string(msgid_obj, msgid_len);
  if (*msgid_len == 0)
    {
      *msgid = "-";
      *msgid_len = 1;
    }
  return msgid_obj;
}

static inline void
_prepend_octet_count(GString *buffer)
{
  gint octet_count = buffer->len;
  gchar octet_count_str[64];
  gsize octet_count_str_len = format_int64_into_padded_buffer(octet_count_str, sizeof(octet_count_str), 0, ' ', 10,
                                                              octet_count);
  g_assert(octet_count_str_len < sizeof(octet_count_str) - 1);
  octet_count_str[octet_count_str_len] = ' ';
  g_string_prepend_len(buffer, octet_count_str, octet_count_str_len + 1);
}

static FilterXObject *
_format_syslog_5424_eval(FilterXExpr *s)
{
  FilterXFunctionFormatSyslog5424 *self = (FilterXFunctionFormatSyslog5424 *) s;

  FilterXEvalContext *context = filterx_eval_get_context();
  LogMessage *logmsg = context->msg;
  FilterXObject *result = NULL;

  gsize message_len;
  const gchar *message;
  FilterXObject *message_obj = _get_message(self, &message, &message_len);
  if (!message_obj)
    return NULL;

  gint64 pri;
  const gchar *pri_str;
  gsize pri_str_len;
  FilterXObject *pri_obj = _get_pri(self, logmsg, &pri, &pri_str, &pri_str_len);

  WallClockTime timestamp;
  FilterXObject *timestamp_obj = _get_timestamp(self, logmsg, &timestamp);

  const gchar *host;
  gsize host_len;
  FilterXObject *host_obj = _get_host(self, &host, &host_len);

  const gchar *program;
  gsize program_len;
  FilterXObject *program_obj = _get_program(self, &program, &program_len);

  const gchar *pid;
  gsize pid_len;
  FilterXObject *pid_obj = _get_pid(self, &pid, &pid_len);

  const gchar *msgid;
  gsize msgid_len;
  FilterXObject *msgid_obj = _get_msgid(self, &msgid, &msgid_len);

  /*                    OCT _   <   PRI >   1   _   TS   _   HOST       _   PROGRAM       _   PID       _ */
  gsize expected_size = 6 + 1 + 1 + 3 + 1 + 1 + 1 + 32 + 1 + host_len + 1 + program_len + 1 + pid_len + 1 +
                        msgid_len + 1 + logmsg->num_sdata * 64 + 1 + 1 + message_len + 1 + 64;
  /*                    MSGID       _   (SDATA or                -)  _   MESSAGE       NL  "for good measure" */

  /* PRI */
  GString *buffer = g_string_sized_new(expected_size);
  g_string_append_c(buffer, '<');
  if (pri != -1)
    format_int64_padded(buffer, 0, ' ', 10, pri);
  else
    g_string_append_len(buffer, pri_str, pri_str_len);
  g_string_append(buffer, ">1 ");

  /* TIMESTAMP */
  append_format_wall_clock_time(&timestamp, buffer, TS_FMT_ISO, 6);
  g_string_append_c(buffer, ' ');

  /* HOST */
  g_string_append_len(buffer, host, host_len);
  g_string_append_c(buffer, ' ');

  /* PROGRAM */
  g_string_append_len(buffer, program, program_len);
  g_string_append_c(buffer, ' ');

  /* PID */
  g_string_append_len(buffer, pid, pid_len);
  g_string_append_c(buffer, ' ');

  /* MSGID */
  g_string_append_len(buffer, msgid, msgid_len);
  g_string_append_c(buffer, ' ');

  /* SDATA */
  if (logmsg->num_sdata)
    log_msg_append_format_sdata(logmsg, buffer, 0);
  else
    g_string_append_c(buffer, '-');

  g_string_append_c(buffer, ' ');

  /* MESSAGE */
  g_string_append_len(buffer, message, message_len);
  g_string_append_c(buffer, '\n');

  if (self->add_octet_count)
    _prepend_octet_count(buffer);

  result = filterx_string_new_take(buffer->str, buffer->len);

  g_string_free(buffer, FALSE);
  filterx_object_unref(message_obj);
  filterx_object_unref(msgid_obj);
  filterx_object_unref(pid_obj);
  filterx_object_unref(program_obj);
  filterx_object_unref(host_obj);
  filterx_object_unref(timestamp_obj);
  filterx_object_unref(pri_obj);

  return result;
}

static FilterXExpr *
_format_syslog_5424_optimize(FilterXExpr *s)
{
  FilterXFunctionFormatSyslog5424 *self = (FilterXFunctionFormatSyslog5424 *) s;

  self->pri_expr = filterx_expr_optimize(self->pri_expr);
  self->timestamp_expr = filterx_expr_optimize(self->timestamp_expr);
  self->host_expr = filterx_expr_optimize(self->host_expr);
  self->program_expr = filterx_expr_optimize(self->program_expr);
  self->pid_expr = filterx_expr_optimize(self->pid_expr);
  self->msgid_expr = filterx_expr_optimize(self->msgid_expr);
  self->message_expr = filterx_expr_optimize(self->message_expr);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_format_syslog_5424_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatSyslog5424 *self = (FilterXFunctionFormatSyslog5424 *) s;

  if (!filterx_expr_init(self->pri_expr, cfg))
    goto pri_error;
  if (!filterx_expr_init(self->timestamp_expr, cfg))
    goto timestamp_error;
  if (!filterx_expr_init(self->host_expr, cfg))
    goto host_error;
  if (!filterx_expr_init(self->program_expr, cfg))
    goto program_error;
  if (!filterx_expr_init(self->pid_expr, cfg))
    goto pid_error;
  if (!filterx_expr_init(self->msgid_expr, cfg))
    goto msgid_error;
  if (!filterx_expr_init(self->message_expr, cfg))
    goto message_error;
  return filterx_function_init_method(&self->super, cfg);

message_error:
  filterx_expr_deinit(self->msgid_expr, cfg);
msgid_error:
  filterx_expr_deinit(self->pid_expr, cfg);
pid_error:
  filterx_expr_deinit(self->program_expr, cfg);
program_error:
  filterx_expr_deinit(self->host_expr, cfg);
host_error:
  filterx_expr_deinit(self->timestamp_expr, cfg);
timestamp_error:
  filterx_expr_deinit(self->pri_expr, cfg);
pri_error:
  return FALSE;
}

static void
_format_syslog_5424_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatSyslog5424 *self = (FilterXFunctionFormatSyslog5424 *) s;

  filterx_expr_deinit(self->pri_expr, cfg);
  filterx_expr_deinit(self->timestamp_expr, cfg);
  filterx_expr_deinit(self->host_expr, cfg);
  filterx_expr_deinit(self->program_expr, cfg);
  filterx_expr_deinit(self->pid_expr, cfg);
  filterx_expr_deinit(self->msgid_expr, cfg);
  filterx_expr_deinit(self->message_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_format_syslog_5424_free(FilterXExpr *s)
{
  FilterXFunctionFormatSyslog5424 *self = (FilterXFunctionFormatSyslog5424 *) s;

  filterx_expr_unref(self->pri_expr);
  filterx_expr_unref(self->timestamp_expr);
  filterx_expr_unref(self->host_expr);
  filterx_expr_unref(self->program_expr);
  filterx_expr_unref(self->pid_expr);
  filterx_expr_unref(self->msgid_expr);
  filterx_expr_unref(self->message_expr);
  filterx_function_free_method(&self->super);
}

static gboolean
_format_syslog_5424_extract_add_octet_count_argument(FilterXFunctionFormatSyslog5424 *self, FilterXFunctionArgs *args,
                                                     GError **error)
{
  gboolean exists, eval_error;
  gboolean add_octet_count = filterx_function_args_get_named_literal_boolean(args, "add_octet_count", &exists,
                                                                             &eval_error);

  if (eval_error)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "\"add_octet_count\" argument must be boolean literal. " FILTERX_FUNC_FORMAT_SYSLOG_5424_USAGE);
      return FALSE;
    }

  if (exists)
    self->add_octet_count = add_octet_count;

  return TRUE;
}

static gboolean
_format_syslog_5424_extract_arguments(FilterXFunctionFormatSyslog5424 *self, FilterXFunctionArgs *args, GError **error)
{
  gsize args_len = filterx_function_args_len(args);
  if (args_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FORMAT_SYSLOG_5424_USAGE);
      return FALSE;
    }

  self->message_expr = filterx_function_args_get_expr(args, 0);
  self->pri_expr = filterx_function_args_get_named_expr(args, "pri");
  self->timestamp_expr = filterx_function_args_get_named_expr(args, "timestamp");
  self->host_expr = filterx_function_args_get_named_expr(args, "host");
  self->program_expr = filterx_function_args_get_named_expr(args, "program");
  self->pid_expr = filterx_function_args_get_named_expr(args, "pid");
  self->msgid_expr = filterx_function_args_get_named_expr(args, "msgid");
  /* SDATA is only supported from $SDATA, currently */

  if (!_format_syslog_5424_extract_add_octet_count_argument(self, args, error))
    return FALSE;

  return TRUE;
}

FilterXExpr *
filterx_function_format_syslog_5424_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFormatSyslog5424 *self = g_new0(FilterXFunctionFormatSyslog5424, 1);
  filterx_function_init_instance(&self->super, "format_syslog_5424", FXE_READ);

  self->super.super.eval = _format_syslog_5424_eval;
  self->super.super.optimize = _format_syslog_5424_optimize;
  self->super.super.init = _format_syslog_5424_init;
  self->super.super.deinit = _format_syslog_5424_deinit;
  self->super.super.free_fn = _format_syslog_5424_free;

  if (!_format_syslog_5424_extract_arguments(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(format_syslog_5424, filterx_function_format_syslog_5424_new);

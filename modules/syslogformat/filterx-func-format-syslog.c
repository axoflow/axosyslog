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

static FilterXObject *
_format_syslog_5424_eval(FilterXExpr *s)
{
  /* TODO: implement */
  return NULL;
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
  filterx_function_init_instance(&self->super, "format_syslog_5424");

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

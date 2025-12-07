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

typedef struct FilterXFunctionFormatSyslog5424_
{
  FilterXFunction super;
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

  return filterx_function_optimize_method(&self->super);
}

static gboolean
_format_syslog_5424_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatSyslog5424 *self = (FilterXFunctionFormatSyslog5424 *) s;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_format_syslog_5424_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatSyslog5424 *self = (FilterXFunctionFormatSyslog5424 *) s;

  filterx_function_deinit_method(&self->super, cfg);
}

static void
_format_syslog_5424_free(FilterXExpr *s)
{
  FilterXFunctionFormatSyslog5424 *self = (FilterXFunctionFormatSyslog5424 *) s;

  filterx_function_free_method(&self->super);
}

static gboolean
_format_syslog_5424_extract_arguments(FilterXFunctionFormatSyslog5424 *self, FilterXFunctionArgs *args, GError **error)
{
  /* TODO: implement */
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

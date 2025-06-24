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

#include "event-format-formatter.h"

#define FILTERX_FUNC_EVENT_FORMAT_FORMATTER_USAGE "Usage: %s(msg_dict)"

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionEventFormatFormatter *self = (FilterXFunctionEventFormatFormatter *) s;

  /* TODO: implement */
  return NULL;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionEventFormatFormatter *self = (FilterXFunctionEventFormatFormatter *) s;

  self->msg = filterx_expr_optimize(self->msg);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionEventFormatFormatter *self = (FilterXFunctionEventFormatFormatter *) s;

  if (!filterx_expr_init(self->msg, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionEventFormatFormatter *self = (FilterXFunctionEventFormatFormatter *) s;

  filterx_expr_deinit(self->msg, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionEventFormatFormatter *self = (FilterXFunctionEventFormatFormatter *) s;

  filterx_expr_unref(self->msg);
  filterx_function_free_method(&self->super);
}

static gboolean
_extract_arguments(FilterXFunctionEventFormatFormatter *self, FilterXFunctionArgs *args, GError **error)
{
  gsize args_len = filterx_function_args_len(args);
  if (args_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_EVENT_FORMAT_FORMATTER_USAGE,
                  self->super.function_name);
      return FALSE;
    }

  self->msg = filterx_function_args_get_expr(args, 0);

  return TRUE;
}

gboolean
filterx_function_event_format_formatter_init_instance(FilterXFunctionEventFormatFormatter *self,
                                                      const gchar *fn_name, FilterXFunctionArgs *args,
                                                      GError **error)
{
  filterx_function_init_instance(&self->super, fn_name);

  self->super.super.eval = _eval;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.free_fn = _free;

  gboolean success = _extract_arguments(self, args, error) && filterx_function_args_check(args, error);

  filterx_function_args_free(args);
  return success;
}

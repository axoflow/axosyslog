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

#include "filterx-func-format-cef.h"
#include "filterx-func-parse-cef.h"
#include "event-format-formatter.h"

FilterXExpr *
filterx_function_format_cef_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionEventFormatFormatter *self = g_new0(FilterXFunctionEventFormatFormatter, 1);

  if (!filterx_function_event_format_formatter_init_instance(self, "format_cef", args, &cef_cfg, error))
    {
      g_free(self);
      return NULL;
    }

  return &self->super.super;
}

FILTERX_FUNCTION(format_cef, filterx_function_format_cef_new);

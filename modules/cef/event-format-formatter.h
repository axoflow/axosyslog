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

#ifndef EVENT_FORMAT_FORMATTER_H_INCLUDED
#define EVENT_FORMAT_FORMATTER_H_INCLUDED

#include "event-format-cfg.h"
#include "filterx/expr-function.h"


typedef struct _FilterXFunctionEventFormatFormatter
{
  FilterXFunction super;
  FilterXExpr *msg;
  Config config;
} FilterXFunctionEventFormatFormatter;

struct _EventFormatterContext
{
  FilterXFunctionEventFormatFormatter *formatter;
  Config config;
};

gboolean event_format_formatter_append_header(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict,
                                              const Field *field);

gboolean filterx_function_event_format_formatter_init_instance(FilterXFunctionEventFormatFormatter *self,
    const gchar *fn_name, FilterXFunctionArgs *args,
    Config *config, GError **error);

#endif

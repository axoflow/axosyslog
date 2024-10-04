/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "filterx-func-parse-cef.h"
#include "event-format-parser.h"

#include "scanner/csv-scanner/csv-scanner.h"
#include "scanner/kv-scanner/kv-scanner.h"

static Field cef_fields[] =
{
  { .name = "version", .field_parser = parse_version},
  { .name = "device_vendor"},
  { .name = "device_product"},
  { .name = "device_version"},
  { .name = "device_event_class_id"},
  { .name = "name"},
  { .name = "agent_severity"},
  { .name = "extensions", .field_parser = parse_extensions},
};

typedef struct FilterXFunctionParseCEF_
{
  FilterXFunctionEventFormatParser super;
} FilterXFunctionParseCEF;

FilterXExpr *
filterx_function_parse_cef_new(FilterXFunctionArgs *args, GError **err)
{
  FilterXFunctionParseCEF *self = g_new0(FilterXFunctionParseCEF, 1);

  Config cfg =
  {
    .signature = "CEF",
    .header = {
      .num_fields = 8,
      .delimiters = "|",
      .fields = cef_fields,
    },
    .extensions = {
      .pair_separator = "  ",
      .value_separator = '=',
    },
  };

  if (!filterx_function_parser_init_instance(&self->super, "parse_cef", args, &cfg, err))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super.super.super;

error:
  append_error_message(err, FILTERX_FUNC_PARSE_CEF_USAGE);
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super.super.super);
  return NULL;
}

FILTERX_GENERATOR_FUNCTION(parse_cef, filterx_function_parse_cef_new);

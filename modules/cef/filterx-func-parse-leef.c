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

#include "filterx-func-parse-leef.h"
#include "event-format-parser.h"

#include "scanner/csv-scanner/csv-scanner.h"
#include "scanner/kv-scanner/kv-scanner.h"

static Field leef_fields[] =
{
  { .name = "version", .field_parser = parse_version},
  { .name = "vendor"},
  { .name = "product_name"},
  { .name = "product_version"},
  { .name = "event_id"},
  { .name = "extensions", .field_parser = parse_extensions},
};

typedef struct FilterXFunctionParseLEEF_
{
  FilterXFunctionEventFormatParser super;
} FilterXFunctionParseLEEF;


FilterXExpr *
filterx_function_parse_leef_new(FilterXFunctionArgs *args, GError **err)
{
  FilterXFunctionParseLEEF *self = g_new0(FilterXFunctionParseLEEF, 1);

  Config cfg =
  {
    .signature = "LEEF",
    .header = {
      .num_fields = 6,
      .delimiters = "|",
      .fields = leef_fields,
    },
    .extensions = {
      .pair_separator = "\t",
      .value_separator = '=',
    },
  };

  if (!filterx_function_parser_init_instance(&self->super, "parse_leef", args, &cfg, err))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super.super.super;

error:
  append_error_message(err, FILTERX_FUNC_PARSE_LEEF_USAGE);
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super.super.super);
  return NULL;
}

FILTERX_GENERATOR_FUNCTION(parse_leef, filterx_function_parse_leef_new);

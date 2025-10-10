/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
 * Copyright (c) 2024 shifter
 *
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

#include <errno.h>
#include <ctype.h>

#include "filterx-func-parse-leef.h"
#include "event-format-parser.h"
#include "filterx-func-format-leef.h"

#include "filterx/object-string.h"

#include "scanner/csv-scanner/csv-scanner.h"
#include "scanner/kv-scanner/kv-scanner.h"
#include "filterx/func-flags.h"

DEFINE_FUNC_FLAGS(FilterXFunctionParseLeefFlags,
                  FILTERX_FUNC_PARSE_LEEF_FLAG_20
                 );

static gboolean
_parse_hex_delimiter(const gchar *hexStr, gchar *delimiter)
{
  errno = 0;
  *delimiter = (gchar)strtol(hexStr, NULL, 16);
  return (errno == 0);
}

static gboolean
_delimiter_multi_parser(const gchar *input, gint input_len, gchar *delimiter)
{
  const gchar *hexStr = NULL;
  switch (input_len)
    {
    case 0:
      return TRUE; // do not change
    case 1:
      *delimiter = input[0];
      return TRUE;
    case 3:
      if (input[0] == 'x' || input[0] == 'X')
        hexStr = &input[1];
      break;
    case 4:
      if (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))
        hexStr = &input[2];
      break;
    default:
      return FALSE; // no match
    }
  if (!hexStr)
    return FALSE;
  return _parse_hex_delimiter(hexStr, delimiter);
}

static gboolean
_is_pair_separator_forced(EventParserContext *ctx)
{
  return ctx->parser->kv_pair_separator != NULL;
}

static gboolean
_is_delmiter_empty(gchar delimiter)
{
  return delimiter == 0;
}

static gboolean
_fallback_to_parse_extensions(EventParserContext *ctx, const gchar *input, gint input_len, FilterXObject *parsed_dict)
{
  if (!csv_scanner_append_rest(ctx->csv_scanner))
    {
      filterx_eval_push_error_static_info("Failed to evaluate parse_leef()", &ctx->parser->super.super,
                                          "Unexpected end of input");
      return FALSE;
    }

  input = csv_scanner_get_current_value(ctx->csv_scanner);
  input_len = csv_scanner_get_current_value_len(ctx->csv_scanner);

  ctx->field_index++;

  return event_format_parser_parse_extensions(ctx, input, input_len, parsed_dict);
}

gboolean
parse_delimiter(EventParserContext *ctx, const gchar *input, gint input_len, FilterXObject *parsed_dict)
{
  FILTERX_STRING_DECLARE_ON_STACK(key, "leef_delimiter", 14);
  FilterXObject *value = NULL;

  if (!input_len)
    {
      if (csv_scanner_has_input_left(ctx->csv_scanner))
        value = filterx_string_new("", 0);
      goto success;
    }

  gchar delimiter = 0;
  if (_delimiter_multi_parser(input, input_len, &delimiter))
    {
      if (_is_delmiter_empty(delimiter))
        {
          value = filterx_string_new("", 0);
          goto success;
        }

      if (!_is_pair_separator_forced(ctx))
        {
          ctx->config.extensions.pair_separator[0] = delimiter;
          ctx->config.extensions.pair_separator[1] = 0;
        }

      value = filterx_string_new(&delimiter, 1);
      goto success;
    }

  FILTERX_STRING_CLEAR_FROM_STACK(key);

  /*
   * delimiter header field is:
   *   1. either missing,
   *   2. or invalid, which might mean it is missing and there is a | in a value
   */
  return _fallback_to_parse_extensions(ctx, input, input_len, parsed_dict);

success:
  if (value)
    {
      filterx_object_set_subscript(parsed_dict, key, &value);
      filterx_object_unref(value);
    }

  filterx_object_unref(key);
  return TRUE;
}

gboolean
parse_leef_version(EventParserContext *ctx, const gchar *value, gint value_len, FilterXObject *parsed_dict)
{
  if (g_strstr_len(value, value_len, "2.0"))
    event_format_parser_context_set_header(ctx, &leef_v2_cfg.header);

  return event_format_parser_parse_version(ctx, value, value_len, parsed_dict);
}

Field leef_v1_fields[] =
{
  { .name = "leef_version", .field_parser = parse_leef_version, .field_formatter = filterx_function_format_leef_format_version},
  { .name = "vendor_name"},
  { .name = "product_name"},
  { .name = "product_version"},
  { .name = "event_id"},
  { .name = "extensions", .field_parser = event_format_parser_parse_extensions},
};

Field leef_v2_fields[] =
{
  { .name = "leef_version", .field_parser = parse_leef_version, .field_formatter = filterx_function_format_leef_format_version},
  { .name = "vendor_name"},
  { .name = "product_name"},
  { .name = "product_version"},
  { .name = "event_id"},
  { .name = "leef_delimiter", .field_parser = parse_delimiter, .field_formatter = filterx_function_format_leef_format_delimiter},
  { .name = "extensions", .field_parser = event_format_parser_parse_extensions},
};

Config leef_v1_cfg =
{
  .signature = "LEEF",
  .header = {
    .num_fields = 6,
    .delimiters = "|",
    .fields = leef_v1_fields,
  },
  .extensions = {
    .pair_separator = "\t",
    .value_separator = '=',
  },
};

Config leef_v2_cfg =
{
  .signature = "LEEF",
  .header = {
    .num_fields = 7,
    .delimiters = "|",
    .fields = leef_v2_fields,
  },
  .extensions = {
    .pair_separator = "\t",
    .value_separator = '=',
  },
};

typedef struct FilterXFunctionParseLEEF_
{
  FilterXFunctionEventFormatParser super;
} FilterXFunctionParseLEEF;


FilterXExpr *
filterx_function_parse_leef_new(FilterXFunctionArgs *args, GError **err)
{
  FilterXFunctionParseLEEF *self = g_new0(FilterXFunctionParseLEEF, 1);

  if (!filterx_function_parser_init_instance(&self->super, "parse_leef", args, &leef_v1_cfg, err))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super.super;

error:
  event_format_parser_append_error_message(err, FILTERX_FUNC_PARSE_LEEF_USAGE);
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super.super);
  return NULL;
}

FILTERX_FUNCTION(parse_leef, filterx_function_parse_leef_new);

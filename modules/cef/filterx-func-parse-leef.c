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

#include <errno.h>
#include <ctype.h>

#include "filterx-func-parse-leef.h"
#include "event-format-parser.h"

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
_delimiter_multi_parser(const gchar *input, gint input_len, gchar *delimiter, GError **error)
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

FilterXObject *
parse_delimiter(EventParserContext *ctx, const gchar *input, gint input_len, GError **error, gpointer user_data)
{
  if (!check_flag(ctx->flags, FILTERX_FUNC_PARSE_LEEF_FLAG_20))
    return NULL;
  gchar delimiter = 0;
  if (_delimiter_multi_parser(input, input_len, &delimiter, error))
    {
      if (_is_delmiter_empty(delimiter))
        return filterx_string_new("", 0);
      if (!_is_pair_separator_forced(ctx))
        {
          ctx->kv_parser_pair_separator[0] = delimiter;
          ctx->kv_parser_pair_separator[1] = 0;
        }
      return filterx_string_new(&delimiter, 1);
    }
  return NULL;
}

FilterXObject *
parse_leef_version(EventParserContext *ctx, const gchar *value, gint value_len, GError **error, gpointer user_data)
{
  if (g_strstr_len(value, value_len, "2.0"))
    set_flag(&ctx->flags, FILTERX_FUNC_PARSE_LEEF_FLAG_20, TRUE);
  return parse_version(ctx, value, value_len, error, user_data); // call base class parser
}

static Field leef_fields[] =
{
  { .name = "version", .field_parser = parse_leef_version},
  { .name = "vendor"},
  { .name = "product_name"},
  { .name = "product_version"},
  { .name = "event_id"},
  { .name = "delimiter", .optional=TRUE, .field_parser = parse_delimiter},
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
      .num_fields = 7,
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

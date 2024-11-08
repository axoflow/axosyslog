/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
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

#ifndef EVENT_FORMAT_PARSER_H_INCLUDED
#define EVENT_FORMAT_PARSER_H_INCLUDED

#include "syslog-ng.h"
#include "str-utils.h"
#include "filterx/filterx-object.h"
#include "filterx/expr-function.h"

#include "scanner/csv-scanner/csv-scanner.h"
#include "parser/parser-expr.h"
#include "scanner/kv-scanner/kv-scanner.h"

#include "event-format-parser-cfg.h"

#define EVENT_FORMAT_PARSER_ERR_NO_LOG_SIGN_MSG "no log signature %s found"
#define EVENT_FORMAT_PARSER_ERR_LOG_SIGN_DIFFERS_MSG "the log signature differs. actual:%s expected:%s"
#define EVENT_FORMAT_PARSER_ERR_MISSING_COLUMNS_MSG "not enough header columns provided. actual:%ld expected:%ld"
#define EVENT_FORMAT_PARSER_ERR_NOT_STRING_INPUT_MSG "input argument must be string"
#define EVENT_FORMAT_PARSER_ERR_EMPTY_STRING "%s must be a non-empty string literal"
#define EVENT_FORMAT_PARSER_ERR_SEPARATOR_MAX_LENGTH_EXCEEDED "%s max length exceeded"

#define EVENT_FORMAT_PARSER_ERROR event_format_parser_error_quark()
GQuark event_format_parser_error_quark(void);

#define EVENT_FORMAT_PARSER_PAIR_SEPARATOR_MAX_LEN 0x05

#define EVENT_FORMAT_PARSER_ARG_NAME_PAIR_SEPARATOR "pair_separator"
#define EVENT_FORMAT_PARSER_ARG_NAME_VALUE_SEPARATOR "value_separator"

enum EventFormatParserError
{
  EVENT_FORMAT_PARSER_ERR_NO_LOG_SIGN,
  EVENT_FORMAT_PARSER_ERR_LOG_SIGN_DIFFERS,
  EVENT_FORMAT_PARSER_ERR_MISSING_COLUMNS,
  EVENT_FORMAT_PARSER_ERR_NOT_STRING_INPUT,
};

struct _FilterXFunctionEventFormatParser
{
  FilterXGeneratorFunction super;
  FilterXExpr *msg;
  CSVScannerOptions csv_opts;
  Config config;
  gchar *kv_pair_separator;
  gchar kv_value_separator;
};

struct _EventParserContext
{
  FilterXFunctionEventFormatParser *parser;
  guint64 num_fields;
  guint64 field_index;
  guint64 column_index;
  CSVScanner *csv_scanner;
  guint64 flags;
  gchar kv_parser_pair_separator[EVENT_FORMAT_PARSER_PAIR_SEPARATOR_MAX_LEN];
  gchar kv_parser_value_separator;
};

gboolean filterx_function_parser_init_instance(FilterXFunctionEventFormatParser *s, const gchar *fn_name,
                                               FilterXFunctionArgs *args, Config *cfg, GError **error);

FilterXObject *parse_version(EventParserContext *ctx, const gchar *value, gint value_len,
                             GError **error,
                             gpointer user_data);
FilterXObject *parse_extensions(EventParserContext *ctx, const gchar *value, gint value_len,
                                GError **error,
                                gpointer user_data);

static inline void append_error_message(GError **error, const char *extra_info)
{
  if (error == NULL || *error == NULL)
    return;

  gchar *new_message = g_strdup_printf("%s %s", (*error)->message, extra_info);
  GError *new_error = g_error_new((*error)->domain, (*error)->code, "%s", new_message);

  g_error_free(*error);
  *error = new_error;

  g_free(new_message);
}

#endif

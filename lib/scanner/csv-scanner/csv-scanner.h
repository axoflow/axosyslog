/*
 * Copyright (c) 2002-2015 Balabit
 * Copyright (c) 1998-2015 Balázs Scheidler
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


#ifndef CSVSCANNER_H_INCLUDED
#define CSVSCANNER_H_INCLUDED

#include "syslog-ng.h"

typedef enum
{
  CSV_SCANNER_ESCAPE_NONE,
  CSV_SCANNER_ESCAPE_BACKSLASH,
  CSV_SCANNER_ESCAPE_BACKSLASH_WITH_SEQUENCES,
  CSV_SCANNER_ESCAPE_DOUBLE_CHAR,
  CSV_SCANNER_ESCAPE_UNQUOTED_DELIMITER,
} CSVScannerDialect;

#define CSV_SCANNER_STRIP_WHITESPACE   0x0001
#define CSV_SCANNER_GREEDY             0x0002

typedef struct _CSVScannerOptions
{
  gchar *delimiters;
  gchar *quotes_start;
  gchar *quotes_end;
  gchar *null_value;
  GList *string_delimiters;
  CSVScannerDialect dialect;
  guint32 flags;
} CSVScannerOptions;

void csv_scanner_options_clean(CSVScannerOptions *options);
void csv_scanner_options_copy(CSVScannerOptions *dst, CSVScannerOptions *src);
gboolean csv_scanner_options_validate(CSVScannerOptions *options, gint expected_columns);

void csv_scanner_options_set_dialect(CSVScannerOptions *options, CSVScannerDialect dialect);
void csv_scanner_options_set_flags(CSVScannerOptions *options, guint32 flags);
void csv_scanner_options_set_delimiters(CSVScannerOptions *options, const gchar *delimiters);
void csv_scanner_options_set_string_delimiters(CSVScannerOptions *options, GList *string_delimiters);
void csv_scanner_options_set_quotes_start_and_end(CSVScannerOptions *options, const gchar *quotes_start,
                                                  const gchar *quotes_end);
void csv_scanner_options_set_quotes(CSVScannerOptions *options, const gchar *quotes);
void csv_scanner_options_set_quote_pairs(CSVScannerOptions *options, const gchar *quote_pairs);
void csv_scanner_options_set_null_value(CSVScannerOptions *options, const gchar *null_value);

typedef struct
{
  CSVScannerOptions *options;
  enum
  {
    CSV_STATE_INITIAL,
    CSV_STATE_COLUMNS,
    CSV_STATE_GREEDY_COLUMN,
    CSV_STATE_PARTIAL_INPUT,
    CSV_STATE_FINISH,
  } state;
  const gchar *src;
  GString *current_value;
  const gchar *current_value_start_pos;
  gint current_column;
  gint expected_columns;
  gchar current_quote;
} CSVScanner;

gint csv_scanner_get_current_column(CSVScanner *self);
const gchar *csv_scanner_get_current_value(CSVScanner *pstate);
gint csv_scanner_get_current_value_len(CSVScanner *self);
gboolean csv_scanner_append_rest(CSVScanner *self);
gboolean csv_scanner_scan_next(CSVScanner *pstate);
gboolean csv_scanner_is_scan_complete(CSVScanner *pstate);
gboolean csv_scanner_has_input_left(CSVScanner *self);
gchar *csv_scanner_dup_current_value(CSVScanner *self);
void csv_scanner_set_expected_columns(CSVScanner *scanner, gint expected_columns);

void csv_scanner_init(CSVScanner *pstate, CSVScannerOptions *options, const gchar *input);
void csv_scanner_deinit(CSVScanner *pstate);

#endif

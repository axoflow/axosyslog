/*
 * Copyright (c) 2015 Balabit
 * Copyright (c) 2015 Balázs Scheidler <balazs.scheidler@balabit.com>
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

#include <criterion/criterion.h>

#include "csvparser.h"
#include "apphook.h"
#include "logmsg/logmsg.h"
#include "string-list.h"
#include "timeutils/misc.h"


LogParser *
_construct_parser(gint max_columns, gint dialect, gchar *delimiters, gchar *quotes, gchar *null_value,
                  const gchar *string_delims[])
{
  LogParser *p;

  p = csv_parser_new(NULL);
  csv_scanner_options_set_dialect(csv_parser_get_scanner_options(p), dialect);
  if (delimiters)
    csv_scanner_options_set_delimiters(csv_parser_get_scanner_options(p), delimiters);
  if (quotes)
    csv_scanner_options_set_quote_pairs(csv_parser_get_scanner_options(p), quotes);
  if (null_value)
    csv_scanner_options_set_null_value(csv_parser_get_scanner_options(p), null_value);

  if (string_delims)
    csv_scanner_options_set_string_delimiters(csv_parser_get_scanner_options(p), string_array_to_list(string_delims));

  return p;
}

static LogMessage *
_construct_msg(const gchar *msg)
{
  LogMessage *logmsg;

  logmsg = log_msg_new_empty();
  log_msg_set_value_by_name(logmsg, "MESSAGE", msg, -1);
  return logmsg;
}


static void
iterate_pattern(LogParser *p, const gchar *input)
{
  LogMessage *msg;
  struct timespec start, end;
  gint i;

  msg = _construct_msg(input);
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (i = 0; i < 100000; i++)
    {
      log_parser_process(p, &msg, NULL, log_msg_get_value(msg, LM_V_MESSAGE, NULL), -1);
    }
  log_msg_unref(msg);

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("      %-90.*s speed: %12.3f msg/sec\n", (int) strlen(input), input, i * 1e6 / timespec_diff_usec(&end, &start));

}

static void
perftest_parser(LogParser *p, const gchar *input)
{
  iterate_pattern(p, input);
  log_pipe_unref(&p->super);
}

Test(csvparser_perf, test_escaped_parsers_performance)
{
  perftest_parser(_construct_parser(3, CSV_SCANNER_ESCAPE_NONE, " ", NULL, "", NULL),
                  "foo bar baz");

  perftest_parser(_construct_parser(-1, CSV_SCANNER_ESCAPE_NONE, " ", NULL, NULL, NULL),
                  "PROXY TCP4 198.51.100.22 203.0.113.7 35646 80");

  perftest_parser(_construct_parser(-1, CSV_SCANNER_ESCAPE_BACKSLASH, " ", "\"\"[]", "-", NULL),
                  "10.100.20.1 - - [31/Dec/2007:00:17:10 +0100] \"GET /cgi-bin/bugzilla/buglist.cgi?keywords_type=allwords&keywords=public&format=simple HTTP/1.1\" 200 2708 \"-\" \"curl/7.15.5 (i486-pc-linux-gnu) libcurl/7.15.5 OpenSSL/0.9.8c zlib/1.2.3 libidn/0.6.5\" 2 bugzilla.balabit");

}

TestSuite(csvparser_perf, .init = app_startup, .fini = app_shutdown);

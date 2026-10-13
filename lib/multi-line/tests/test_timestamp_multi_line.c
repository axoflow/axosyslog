/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include <criterion/parameterized.h>

#include "multi-line/timestamp-multi-line.h"
#include "apphook.h"
#include "reloc.h"

#include <string.h>
#include <unistd.h>

static gboolean
_starts_with_timestamp(const gchar *pairs, const gchar *line)
{
  MultiLineLogic *mll = timestamp_multi_line_new(pairs);
  gboolean result = timestamp_multi_line_scan(mll, (const guchar *) line, strlen(line));

  multi_line_logic_free(mll);
  return result;
}

typedef struct _ScanCase
{
  gchar line[96];
  gboolean expected;
} ScanCase;

ParameterizedTestParameters(timestamp_multi_line, builtin_and_file_formats)
{
  static ScanCase cases[] =
  {
    /* built-in scanners */
    { "2026-10-09 15:31:50.201 ERROR 1 --- [nio-8080-exec-2] c.e.orders.service.PaymentService", TRUE },
    { "2026-10-09T15:31:50.123456+02:00 message", TRUE },
    { "2026-10-09 15:31:50,123 message", TRUE },
    { "Oct  9 15:31:50 host program[123]: message", TRUE },
    { "Oct 09 2026 15:31:50: %ASA-6-302013: message", TRUE },
    { "Oct 09 15:31:50 2026 message", TRUE },
    /* syslog relayed by a forwarder, with and without its PRI */
    { "<134>Oct  9 15:31:50 host program[123]: message", TRUE },
    { "<13>1 2026-10-09T15:31:50.123+02:00 host app 42 - - message", TRUE },
    { "1 2026-10-09T15:31:50Z host app - - - message", TRUE },
    { "<13>[2026-10-09 15:31:50] message", TRUE },
    { "<1234>Oct  9 15:31:50 not a PRI", FALSE },
    { "<>Oct  9 15:31:50 not a PRI", FALSE },
    { "<134>", FALSE },
    { "1 message", FALSE },
    /* built-in application log scanners */
    { "10-09-2026 15:31:50.456 +0000 INFO  TailReader [101 MainTailingThread] - message", TRUE },
    { "10-09-2026 15:31:50 message", TRUE },
    { "Thu Oct 09 15:31:50.123456 2026 [core:notice] message", TRUE },
    { "Thu Oct 09 15:31:50 2026 message", TRUE },
    { "09/Oct/2026:15:31:50 +0200 message", TRUE },
    { "2026/10/09 15:31:50 [error] message", TRUE },
    { "Oct 09, 2026 3:31:50 PM com.example.Main run", TRUE },
    { "09 Oct 2026 15:31:50 message", TRUE },
    { "Thu, 09 Oct 2026 15:31:50 message", TRUE },
    { "09-Oct-2026 15:31:50.123 INFO [main] message", TRUE },
    { "10/09/2026 15:31:50 message", TRUE },
    { "09.10.2026 15:31:50 message", TRUE },
    { "09 October 2026 15:31:50 message", TRUE },
    { "October 09, 2026 15:31:50 message", TRUE },
    /* commented out in the formats file */
    { "1791561960 message", FALSE },
    { "1791561960.123456 message", FALSE },
    { "20261009 153150 message", FALSE },
    { "15:31:50 message", FALSE },
    { "2026-10-09 message", FALSE },
    /* continuation lines */
    { "\tat com.example.orders.payment.StripeClient.authorize(StripeClient.java:142)", FALSE },
    { "Caused by: java.net.SocketTimeoutException: Read timed out", FALSE },
    { "com.example.orders.payment.PaymentGatewayException: Timeout", FALSE },
    { "... 21 common frames omitted", FALSE },
    { "  Detail: Key (customer_id)=(31337) is not present in table \"customers\".", FALSE },
    { "", FALSE },
    { "2026-1", FALSE },
    { " 2026-10-09 15:31:50 indented timestamp", FALSE },
  };

  return cr_make_param_array(ScanCase, cases, G_N_ELEMENTS(cases));
}

ParameterizedTest(ScanCase *c, timestamp_multi_line, builtin_and_file_formats)
{
  cr_assert_eq(_starts_with_timestamp(NULL, c->line), c->expected, "line: '%s'", c->line);
}

/* the installed file enables nothing, so the strptime() path is exercised
 * with a file of this test's own */
Test(timestamp_multi_line, the_formats_file_adds_strptime_formats)
{
  gchar *filename = g_build_filename(g_get_tmp_dir(), "timestamp-multi-line-test.formats", NULL);
  GError *error = NULL;

  cr_assert(g_file_set_contents(filename,
                                "# epoch seconds, then a format no scanner knows\n"
                                "%s\n"
                                "\n"
                                "  %Y%m%d %H%M%S  \n", -1, &error), "cannot write %s", filename);
  timestamp_multi_line_load_formats(filename);

  cr_assert(_starts_with_timestamp(NULL, "1791561960 message"));
  cr_assert(_starts_with_timestamp(NULL, "20261009 153150 message"));
  cr_assert(_starts_with_timestamp(NULL, "[1791561960] message"));
  cr_assert_not(_starts_with_timestamp(NULL, "message 1791561960"));

  /* an unreadable file leaves only the built-in scanners */
  unlink(filename);
  timestamp_multi_line_load_formats(filename);
  cr_assert_not(_starts_with_timestamp(NULL, "1791561960 message"));
  cr_assert(_starts_with_timestamp(NULL, "2026-10-09 15:31:50 message"));

  g_free(filename);
}

Test(timestamp_multi_line, the_default_pairs_enclose_the_timestamp_in_square_brackets)
{
  cr_assert(_starts_with_timestamp(NULL, "[2026-10-09 15:31:50] message"));
  cr_assert(_starts_with_timestamp(NULL, "[2026-10-09 15:31:50.123] message"));
  cr_assert(_starts_with_timestamp(NULL, "[Thu Oct 09 15:31:50.123456 2026] [core:notice] message"));
  cr_assert(_starts_with_timestamp(NULL, "[09/Oct/2026:15:31:50 +0200] \"GET / HTTP/1.1\""));

  /* the pair has to be closed right after the timestamp */
  cr_assert_not(_starts_with_timestamp(NULL, "[2026-10-09 15:31:50 message"));
  cr_assert_not(_starts_with_timestamp(NULL, "[2026-10-09 15:31:50 INFO] message"));
  cr_assert_not(_starts_with_timestamp(NULL, "[2026-10-09 15:31:50"));

  /* other brackets are not pairs by default */
  cr_assert_not(_starts_with_timestamp(NULL, "(2026-10-09 15:31:50) message"));
  cr_assert_not(_starts_with_timestamp(NULL, "<2026-10-09 15:31:50> message"));
}

Test(timestamp_multi_line, pairs_can_be_configured)
{
  cr_assert(_starts_with_timestamp("[]()<>", "(2026-10-09 15:31:50) message"));
  cr_assert(_starts_with_timestamp("[]()<>", "<2026-10-09 15:31:50> message"));
  cr_assert(_starts_with_timestamp("[]()<>", "[2026-10-09 15:31:50] message"));
  cr_assert_not(_starts_with_timestamp("[]()<>", "(2026-10-09 15:31:50] message"));

  /* no pairs at all: a bracket is just another character */
  cr_assert_not(_starts_with_timestamp("", "[2026-10-09 15:31:50] message"));
  cr_assert(_starts_with_timestamp("", "2026-10-09 15:31:50 message"));
}

/****************************************************************************
 * Accumulating lines
 ****************************************************************************/

static GString *buffer;
static GPtrArray *output_messages;

static void
_feed_line(MultiLineLogic *mll, const gchar *input)
{
  gsize input_len = strlen(input);
  gboolean repeat;

  do
    {
      gint verdict = multi_line_logic_accumulate_line(mll, (const guchar *) buffer->str, buffer->len,
                                                      (const guchar *) input, input_len);

      repeat = FALSE;
      if (verdict & MLL_CONSUME_SEGMENT)
        {
          if (buffer->len > 0)
            g_string_append_c(buffer, '\n');
          g_string_append_len(buffer, input, input_len);
          if (verdict & MLL_EXTRACTED)
            {
              g_ptr_array_add(output_messages, g_strdup(buffer->str));
              g_string_truncate(buffer, 0);
            }
        }
      else if (verdict & MLL_REWIND_SEGMENT)
        {
          cr_assert(verdict & MLL_EXTRACTED);
          g_ptr_array_add(output_messages, g_strdup(buffer->str));
          g_string_truncate(buffer, 0);
          repeat = TRUE;
        }
      else
        cr_assert_fail("unexpected verdict %d", verdict);
    }
  while (repeat);
}

static void
_feed_lines(MultiLineLogic *mll, const gchar *lines[])
{
  for (gint i = 0; lines[i]; i++)
    _feed_line(mll, lines[i]);

  /* the last event is pending until the next timestamp */
  _feed_line(mll, "2099-01-01 00:00:00 END");
  if (buffer->len)
    {
      g_ptr_array_add(output_messages, g_strdup(buffer->str));
      g_string_truncate(buffer, 0);
    }
}

static const gchar *
_output(gint ndx)
{
  if (output_messages->len <= ndx)
    return "<unset>";
  return g_ptr_array_index(output_messages, ndx);
}

Test(timestamp_multi_line, lines_without_a_timestamp_continue_the_previous_event)
{
  MultiLineLogic *mll = timestamp_multi_line_new(NULL);
  const gchar *lines[] =
  {
    "2026-10-09 15:31:40.188  INFO 1 --- [nio-8080-exec-2] c.e.orders.service.PaymentService        : Authorizing",
    "2026-10-09 15:31:50.201 ERROR 1 --- [nio-8080-exec-2] c.e.orders.service.PaymentService        : Payment failed",
    "com.example.orders.payment.PaymentGatewayException: Timeout while contacting payment provider 'stripe'",
    "\tat com.example.orders.payment.StripeClient.authorize(StripeClient.java:142) ~[orders.jar:2.14.3]",
    "Caused by: java.net.SocketTimeoutException: Read timed out",
    "\tat java.base/sun.nio.ch.NioSocketImpl.timedRead(NioSocketImpl.java:288) ~[na:na]",
    "\t... 21 common frames omitted",
    "2026-10-09 15:31:50.209  INFO 1 --- [nio-8080-exec-2] c.e.orders.web.OrderController           : POST -> 502",
    "[2026-10-09 15:32:00.001] bracketed timestamps start events as well",
    "  Detail: and indented lines continue them",
    NULL,
  };

  _feed_lines(mll, lines);

  cr_assert_eq(output_messages->len, 5, "unexpected number of events: %d", output_messages->len);
  cr_assert_str_eq(_output(0), lines[0]);
  cr_assert_str_eq(_output(1),
                   "2026-10-09 15:31:50.201 ERROR 1 --- [nio-8080-exec-2] c.e.orders.service.PaymentService        : Payment failed\n"
                   "com.example.orders.payment.PaymentGatewayException: Timeout while contacting payment provider 'stripe'\n"
                   "\tat com.example.orders.payment.StripeClient.authorize(StripeClient.java:142) ~[orders.jar:2.14.3]\n"
                   "Caused by: java.net.SocketTimeoutException: Read timed out\n"
                   "\tat java.base/sun.nio.ch.NioSocketImpl.timedRead(NioSocketImpl.java:288) ~[na:na]\n"
                   "\t... 21 common frames omitted");
  cr_assert_str_eq(_output(2), lines[7]);
  cr_assert_str_eq(_output(3),
                   "[2026-10-09 15:32:00.001] bracketed timestamps start events as well\n"
                   "  Detail: and indented lines continue them");
  cr_assert_str_eq(_output(4), "2099-01-01 00:00:00 END");

  multi_line_logic_free(mll);
}

Test(timestamp_multi_line, the_first_line_starts_an_event_even_without_a_timestamp)
{
  MultiLineLogic *mll = timestamp_multi_line_new(NULL);
  const gchar *lines[] =
  {
    "no timestamp here",
    "nor here",
    "2026-10-09 15:31:50 but here",
    NULL,
  };

  _feed_lines(mll, lines);

  cr_assert_eq(output_messages->len, 3);
  cr_assert_str_eq(_output(0), "no timestamp here\nnor here");
  cr_assert_str_eq(_output(1), "2026-10-09 15:31:50 but here");

  multi_line_logic_free(mll);
}

static void
setup(void)
{
  /* the formats file is not installed yet when the tests run from the build tree */
  override_installation_path_for("${pkgdatadir}/timestamp-multi-line.formats",
                                 TOP_SRCDIR "/lib/multi-line/timestamp-multi-line.formats");
  app_startup();
  buffer = g_string_new(NULL);
  output_messages = g_ptr_array_new_with_free_func(g_free);
}

static void
teardown(void)
{
  g_ptr_array_free(output_messages, TRUE);
  g_string_free(buffer, TRUE);
  app_shutdown();
}

TestSuite(timestamp_multi_line, .init = setup, .fini = teardown);

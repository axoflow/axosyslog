/*
 * Copyright (c) 2012-2019 Balabit
 * Copyright (c) 2012-2013 Balázs Scheidler
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
#include "libtest/mock-transport.h"
#include "libtest/proto_lib.h"
#include "libtest/msg_parse_lib.h"

#include "multi-line/multi-line-factory.h"
#include "logproto/logproto-multiline-server.h"
#include "logproto/logproto-text-server.h"

#include <iv.h>
#include <unistd.h>

static LogProtoServer *
_indented_multiline_server_new(LogTransport *transport, const LogProtoServerOptions *options, gint timeout_msec)
{
  MultiLineOptions multi_line_options;

  multi_line_options_defaults(&multi_line_options);
  multi_line_options_set_mode(&multi_line_options, "indented");
  multi_line_options_set_timeout(&multi_line_options, timeout_msec);
  LogProtoServer *server = log_proto_multiline_server_new(transport, options, &multi_line_options);
  multi_line_options_destroy(&multi_line_options);
  return server;
}

LogProtoServer *
log_proto_indented_multiline_server_new(LogTransport *transport, const LogProtoServerOptions *options)
{
  return _indented_multiline_server_new(transport, options, 0);
}

static void
test_proper_multiline(LogTransportMockConstructor log_transport_mock_new)
{
  LogProtoServer *proto;

  proto_server_options.max_msg_size = 32;

  proto = log_proto_indented_multiline_server_new(
            /* 32 bytes max line length */
            log_transport_mock_new(
              "0\n"
              " 1=2\n"
              " 3=4\n"
              "newline\n", -1,
              LTM_PADDING,
              LTM_EOF),
            get_inited_proto_server_options());

  assert_proto_server_fetch(&proto, "0\n"
                                    " 1=2\n"
                                    " 3=4", -1);

  log_proto_server_free(proto);
}

Test(log_proto, test_proper_multiline)
{
  test_proper_multiline(log_transport_mock_stream_new);
  test_proper_multiline(log_transport_mock_records_new);
}

static void
test_line_without_continuation(LogTransportMockConstructor log_transport_mock_new)
{
  LogProtoServer *proto;

  proto_server_options.max_msg_size = 32;

  proto = log_proto_indented_multiline_server_new(
            /* 32 bytes max line length */
            log_transport_mock_new(
              "01234567\n", -1,
              "01234567\n", -1,
              "newline\n", -1,
              LTM_PADDING,
              LTM_EOF),
            get_inited_proto_server_options());

  assert_proto_server_fetch(&proto, "01234567", -1);
  assert_proto_server_fetch(&proto, "01234567", -1);

  log_proto_server_free(proto);
}

Test(log_proto, test_line_without_continuation)
{
  test_line_without_continuation(log_transport_mock_stream_new);
  test_line_without_continuation(log_transport_mock_records_new);
}

static void
test_input_starts_with_continuation(LogTransportMockConstructor log_transport_mock_new)
{
  LogProtoServer *proto;

  proto_server_options.max_msg_size = 32;

  proto = log_proto_indented_multiline_server_new(
            /* 32 bytes max line length */
            log_transport_mock_new(
              " 01234567\n", -1,
              "01234567\n", -1,
              "newline\n", -1,
              LTM_PADDING,
              LTM_EOF),
            get_inited_proto_server_options());

  assert_proto_server_fetch(&proto, " 01234567", -1);
  assert_proto_server_fetch(&proto, "01234567", -1);

  log_proto_server_free(proto);
}

Test(log_proto, test_input_starts_with_continuation)
{
  test_input_starts_with_continuation(log_transport_mock_stream_new);
  test_input_starts_with_continuation(log_transport_mock_records_new);
}

static void
test_multiline_at_eof(LogTransportMockConstructor log_transport_mock_new)
{
  LogProtoServer *proto;

  proto_server_options.max_msg_size = 32;

  proto = log_proto_indented_multiline_server_new(
            /* 32 bytes max line length */
            log_transport_mock_new(
              "01234567\n", -1,
              " 01234567\n", -1,
              " end\n", -1,
              LTM_EOF),
            get_inited_proto_server_options());

  assert_proto_server_fetch(&proto, "01234567\n"
                                    " 01234567\n"
                                    " end", -1);
  assert_proto_server_fetch_failure(&proto, LPS_EOF, NULL);

  log_proto_server_free(proto);
}

Test(log_proto, test_multiline_at_eof)
{
  test_multiline_at_eof(log_transport_mock_stream_new);
  test_multiline_at_eof(log_transport_mock_records_new);
}

/* iv_now is read once per loop iteration, and there is no loop here */
static void
_sleep_msec(gint msec)
{
  usleep(msec * 1000);
  iv_invalidate_now();
}

/* one fetch(), unlike proto_server_fetch() which keeps going until a message */
static LogProtoStatus
_fetch_once(LogProtoServer *proto, const guchar **msg, gsize *msg_len)
{
  LogTransportAuxData aux;
  gboolean may_read = TRUE;

  *msg = NULL;
  *msg_len = 0;
  log_transport_aux_data_init(&aux);
  LogProtoStatus status = log_proto_server_fetch(proto, msg, msg_len, &may_read, &aux, NULL);
  log_transport_aux_data_destroy(&aux);
  return status;
}

Test(log_proto, test_multi_line_timeout_hands_over_the_pending_lines, .timeout = 10)
{
  LogProtoServer *proto;
  const guchar *msg = NULL;
  gsize msg_len = 0;
  GIOCondition cond;
  gint timeout;

  proto_server_options.max_msg_size = 32;
  /* the stream goes quiet after the second line, nothing ends the message */
  proto = _indented_multiline_server_new(
            log_transport_mock_endless_records_new(
              "0\n"
              " 1=2\n", -1,
              LTM_EOF),
            get_inited_proto_server_options(), 100);

  /* the lines are read in and consumed, then the quiet stream reports EAGAIN */
  cr_assert_eq(_fetch_once(proto, &msg, &msg_len), LPS_AGAIN);
  cr_assert_null(msg, "the message must wait for its next line");

  /* the timer firing before the deadline changes nothing */
  log_proto_text_server_fire_multi_line_timeout(proto);
  cr_assert_eq(log_proto_server_poll_prepare(proto, &cond, &timeout), LPPA_POLL_IO);

  /* past the deadline the lines consumed so far are flushed as the message */
  _sleep_msec(150);
  log_proto_text_server_fire_multi_line_timeout(proto);
  cr_assert_eq(log_proto_server_poll_prepare(proto, &cond, &timeout), LPPA_FORCE_SCHEDULE_FETCH);
  cr_assert_eq(_fetch_once(proto, &msg, &msg_len), LPS_SUCCESS);
  cr_assert_not_null(msg);
  cr_assert_eq(msg_len, strlen("0\n 1=2\n"));
  cr_assert_arr_eq(msg, "0\n 1=2\n", msg_len);

  cr_assert_eq(log_proto_server_poll_prepare(proto, &cond, &timeout), LPPA_POLL_IO);

  log_proto_server_free(proto);
}

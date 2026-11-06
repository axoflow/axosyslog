/*
 * Copyright (c) 2026 Axoflow
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
#include "libtest/mock-transport.h"

#include "logproto/logproto-framed-client.h"
#include "apphook.h"

#include <string.h>

typedef struct _FlowControlCounters
{
  gint acked;
  gint rewound;
} FlowControlCounters;

static LogProtoClientOptionsStorage options;
static FlowControlCounters counters;

static void
_ack(gint num_msg_acked, gpointer user_data)
{
  ((FlowControlCounters *) user_data)->acked += num_msg_acked;
}

static void
_rewind(gpointer user_data)
{
  ((FlowControlCounters *) user_data)->rewound++;
}

static LogProtoClient *
_construct(LogProtoClient *(*construct)(LogTransport *, const LogProtoClientOptions *),
           LogTransport *transport, gint flush_lines)
{
  options.super.flush_lines = flush_lines;
  LogProtoClient *proto = construct(transport, &options.super);

  LogProtoClientFlowControlFuncs funcs = { .ack_callback = _ack, .rewind_callback = _rewind, .user_data = &counters };
  log_proto_client_set_client_flow_control(proto, &funcs);
  return proto;
}

static gboolean
_post(LogProtoClient *proto, const gchar *msg)
{
  gboolean consumed = FALSE;

  cr_assert_neq(log_proto_client_post(proto, NULL, (const guchar *) msg, strlen(msg), &consumed), LPS_ERROR);
  return consumed;
}

static void
_flush(LogProtoClient *proto)
{
  for (gint i = 0; i < 100; i++)
    cr_assert_neq(log_proto_client_flush(proto), LPS_ERROR);
}

/* post all messages, flushing whenever the client asks the caller to resend */
static void
_post_all(LogProtoClient *proto, const gchar *msgs[])
{
  for (gint i = 0; msgs[i]; i++)
    {
      gint attempts = 0;
      while (!_post(proto, msgs[i]))
        {
          cr_assert_lt(++attempts, 100, "the client never accepted message %d", i);
          cr_assert_neq(log_proto_client_flush(proto), LPS_ERROR);
        }
    }
}

static void
_assert_written(LogTransportMock *transport, const gchar *expected)
{
  gchar buf[1024];
  gssize len = log_transport_mock_read_from_write_buffer(transport, buf, sizeof(buf) - 1);

  buf[len] = 0;
  cr_assert_str_eq(buf, expected);
}

Test(framed_client, messages_are_framed)
{
  LogTransport *transport = log_transport_mock_stream_new(NULL, 0);
  LogProtoClient *proto = _construct(log_proto_framed_client_new, transport, 3);

  const gchar *msgs[] = { "abc", "de", "fghij", NULL };
  _post_all(proto, msgs);
  _flush(proto);

  _assert_written((LogTransportMock *) transport, "3 abc2 de5 fghij");
  cr_assert_eq(counters.acked, 3);

  log_proto_client_free(proto);
}

Test(framed_client, short_writes_keep_the_frames_intact)
{
  LogTransport *transport = log_transport_mock_stream_new(NULL, 0);
  log_transport_mock_set_write_chunk_limit((LogTransportMock *) transport, 2);
  LogProtoClient *proto = _construct(log_proto_framed_client_new, transport, 3);

  const gchar *msgs[] = { "abc", "defghijklmn", "o", "pq", NULL };
  _post_all(proto, msgs);
  _flush(proto);

  _assert_written((LogTransportMock *) transport, "3 abc11 defghijklmn1 o2 pq");
  cr_assert_eq(counters.acked, 4);

  log_proto_client_free(proto);
}

Test(framed_client, a_message_is_acked_once_its_frame_is_written_completely)
{
  LogTransport *transport = log_transport_mock_stream_new(NULL, 0);
  log_transport_mock_set_write_chunk_limit((LogTransportMock *) transport, 2);
  LogProtoClient *proto = _construct(log_proto_framed_client_new, transport, 1);

  cr_assert(_post(proto, "abc"));
  _assert_written((LogTransportMock *) transport, "3 ");
  cr_assert_eq(counters.acked, 0, "the message must not be acked once only its header is written");

  cr_assert_neq(log_proto_client_flush(proto), LPS_ERROR);
  cr_assert_eq(counters.acked, 0);
  cr_assert_neq(log_proto_client_flush(proto), LPS_ERROR);
  _assert_written((LogTransportMock *) transport, "abc");
  cr_assert_eq(counters.acked, 1);

  log_proto_client_free(proto);
  cr_assert_eq(counters.rewound, 0);
}

static void
setup(void)
{
  app_startup();
  memset(&counters, 0, sizeof(counters));
  log_proto_client_options_defaults(&options.super);
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(framed_client, .init = setup, .fini = teardown);

/*
 * Copyright (c) 2026 Adam Kiss
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

#include "splunk-s2s-proto-client.h"
#include "splunk-s2s-protocol.h"

#include "apphook.h"
#include "logmsg/logmsg.h"
#include "scratch-buffers.h"

#include <string.h>
#include <errno.h>

/* an arbitrary v3 frame standing in for the indexer's signature reply: a
 * BE-u32 length prefix and as many payload bytes, the client discards it */
#define SERVER_HELLO "\x00\x00\x00\x05HELLO"
#define SERVER_HELLO_LEN 9

static gint acks;
static gint rewinds;

static void
_ack(gint num_msg_acked, gpointer user_data)
{
  acks += num_msg_acked;
}

static void
_rewind(gpointer user_data)
{
  rewinds++;
}

static LogProtoClientOptionsStorage options_storage;

static LogProtoClient *
_client_new(LogTransport *transport)
{
  LogProtoClient *client = log_proto_splunk_s2s_client_new(transport, &options_storage.super);
  LogProtoClientFlowControlFuncs flow_control_funcs =
  {
    .ack_callback = _ack,
    .rewind_callback = _rewind,
  };

  log_proto_client_set_client_flow_control(client, &flow_control_funcs);
  return client;
}

static GString *
_expected_hello(void)
{
  GString *expected = g_string_new(NULL);

  splunk_s2s_format_header1(expected, "axosyslog", "0");
  splunk_s2s_format_v3_signature_frame(expected, SPLUNK_S2S_CAPABILITIES_SIGNATURE_ACK);
  return expected;
}

static GString *
_expected_default_channel(void)
{
  GString *expected = g_string_new(NULL);

  splunk_s2s_format_open_channel(expected, 1, "axosyslog", g_get_host_name(), "axosyslog");
  return expected;
}

static GString *
_written_bytes(LogTransport *transport)
{
  GString *written = g_string_sized_new(4096);
  gchar buf[1024];
  gssize rc;

  while ((rc = log_transport_mock_read_from_write_buffer((LogTransportMock *) transport, buf, sizeof(buf))) > 0)
    g_string_append_len(written, buf, rc);
  return written;
}

/* g_strstr_len() stops at the first NUL of the haystack, the v3 frames are
 * full of them */
static gboolean
_contains_bytes(const gchar *haystack, gsize haystack_len, const gchar *needle)
{
  gsize needle_len = strlen(needle);

  if (needle_len > haystack_len)
    return FALSE;

  for (gsize i = 0; i + needle_len <= haystack_len; i++)
    {
      if (memcmp(haystack + i, needle, needle_len) == 0)
        return TRUE;
    }
  return FALSE;
}

static void
_assert_bytes_eq(const gchar *actual, gsize actual_len, const gchar *expected, gsize expected_len)
{
  for (gsize i = 0; i < MIN(actual_len, expected_len); i++)
    {
      cr_assert_eq((guint8) actual[i], (guint8) expected[i],
                   "byte mismatch at offset %" G_GSIZE_FORMAT ": actual 0x%02x, expected 0x%02x",
                   i, (guint8) actual[i], (guint8) expected[i]);
    }
  cr_assert_eq(actual_len, expected_len,
               "length mismatch: actual %" G_GSIZE_FORMAT ", expected %" G_GSIZE_FORMAT,
               actual_len, expected_len);
}

/* the proto stops asking for handshake I/O once it is connected and idle */
static gboolean
_handshake_done(LogProtoClient *client)
{
  GIOCondition cond, idle_cond;
  gint timeout = -1;

  return !log_proto_client_poll_prepare(client, &cond, &idle_cond, &timeout);
}

static gboolean
_run_handshake(LogProtoClient *client, gint max_steps)
{
  for (gint i = 0; i < max_steps && !_handshake_done(client); i++)
    {
      cr_assert_eq(log_proto_client_flush(client), LPS_SUCCESS);
      if (_handshake_done(client))
        break;
      cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
    }
  return _handshake_done(client);
}

static void
_assert_handshake_output(LogTransport *transport)
{
  GString *written = _written_bytes(transport);
  GString *hello = _expected_hello();
  GString *channel = _expected_default_channel();

  cr_assert_gt(written->len, hello->len + channel->len);

  _assert_bytes_eq(written->str, hello->len, hello->str, hello->len);

  /* the forwarder info v3 frame: length prefixed, advertises v4 */
  const gchar *info = written->str + hello->len;
  gsize info_len = written->len - hello->len - channel->len;
  guint32 frame_len;
  cr_assert(splunk_s2s_parse_v3_frame_len((const guchar *) info, info_len, &frame_len));
  cr_assert_eq(frame_len, info_len - 4, "forwarder info frame length prefix mismatch");
  cr_assert(_contains_bytes(info, info_len, "v4=1"), "forwarder info does not advertise v4");

  _assert_bytes_eq(written->str + written->len - channel->len, channel->len, channel->str, channel->len);

  g_string_free(written, TRUE);
  g_string_free(hello, TRUE);
  g_string_free(channel, TRUE);
}

static void
setup(void)
{
  app_startup();
  log_proto_client_options_defaults(&options_storage.super);
  acks = 0;
  rewinds = 0;
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(splunk_s2s_proto_client, .init = setup, .fini = teardown);

Test(splunk_s2s_proto_client, handshake_completes_against_scripted_indexer)
{
  /* stream mock: reads trickle in one byte at a time */
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _client_new(transport);

  cr_assert(_run_handshake(client, 100), "handshake did not finish");
  _assert_handshake_output(transport);
  cr_assert_eq(acks, 0, "handshake frames must not be acked as messages");

  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, handshake_resumes_across_eagain)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(
                              LTM_INJECT_ERROR(EAGAIN),
                              SERVER_HELLO, 2,
                              LTM_INJECT_ERROR(EAGAIN),
                              SERVER_HELLO + 2, SERVER_HELLO_LEN - 2,
                              LTM_EOF);
  LogProtoClient *client = _client_new(transport);

  cr_assert_eq(log_proto_client_flush(client), LPS_SUCCESS);
  cr_assert_not(_handshake_done(client), "handshake finished before the reply arrived");

  cr_assert(_run_handshake(client, 100), "handshake did not finish");
  _assert_handshake_output(transport);

  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, handshake_survives_short_writes)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  log_transport_mock_set_write_chunk_limit((LogTransportMock *) transport, 7);
  LogProtoClient *client = _client_new(transport);

  cr_assert(_run_handshake(client, 200), "handshake did not finish");
  _assert_handshake_output(transport);

  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, handshake_rejects_oversized_reply)
{
  LogTransport *transport = log_transport_mock_endless_stream_new("\x7f\xff\xff\xff", 4, LTM_EOF);
  LogProtoClient *client = _client_new(transport);

  cr_assert_eq(log_proto_client_flush(client), LPS_ERROR);

  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, handshake_detects_eof)
{
  /* non-endless mock: exhausting the scripted data is an EOF */
  LogTransport *transport = log_transport_mock_stream_new(SERVER_HELLO, 3, LTM_EOF);
  LogProtoClient *client = _client_new(transport);

  cr_assert_eq(log_proto_client_flush(client), LPS_ERROR);

  log_proto_client_free(client);
}

static LogMessage *
_test_msg_new(void)
{
  LogMessage *msg = log_msg_new_empty();

  msg->timestamps[LM_TS_STAMP].ut_sec = 1700000000;
  return msg;
}

static GString *
_expected_event(guint64 event_id, const gchar *index, const gchar *sourcetype, const gchar *source,
                const gchar *host, const gchar *raw)
{
  GString *sourcetype_buf = g_string_new(NULL);
  GString *source_buf = g_string_new(NULL);
  GString *host_buf = g_string_new(NULL);

  g_string_printf(sourcetype_buf, "sourcetype::%s", sourcetype);
  g_string_printf(source_buf, "source::%s", source);
  g_string_printf(host_buf, "host::%s", host);

  SplunkS2SEventField fields[] =
  {
    { .name = "_MetaData:Index", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = index, .str_value_len = strlen(index) },
    { .name = "MetaData:Sourcetype", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = sourcetype_buf->str, .str_value_len = sourcetype_buf->len },
    { .name = "MetaData:Source", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = source_buf->str, .str_value_len = source_buf->len },
    { .name = "MetaData:Host", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = host_buf->str, .str_value_len = host_buf->len },
  };

  GString *expected = g_string_new(NULL);
  splunk_s2s_format_event(expected, 1, SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER, 1700000000, event_id,
                          fields, G_N_ELEMENTS(fields), raw, strlen(raw));

  g_string_free(sourcetype_buf, TRUE);
  g_string_free(source_buf, TRUE);
  g_string_free(host_buf, TRUE);
  return expected;
}

Test(splunk_s2s_proto_client, post_encodes_event_with_metadata_nvpairs)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _client_new(transport);

  cr_assert(_run_handshake(client, 100));
  g_string_free(_written_bytes(transport), TRUE);

  LogMessage *msg = _test_msg_new();
  log_msg_set_value_by_name(msg, ".splunk.index", "custom-index", -1);
  log_msg_set_value_by_name(msg, ".splunk.source", "custom-source", -1);
  log_msg_set_value_by_name(msg, ".splunk.sourcetype", "custom-sourcetype", -1);
  log_msg_set_value_by_name(msg, ".splunk.host", "custom-host", -1);

  const gchar *raw = "the raw payload";
  gboolean consumed = FALSE;
  cr_assert_eq(log_proto_client_post(client, msg, (guchar *) g_strdup(raw), strlen(raw), &consumed), LPS_SUCCESS);
  cr_assert(consumed);
  cr_assert_eq(acks, 0, "no writer ack before the indexer confirms");

  GString *written = _written_bytes(transport);
  GString *expected = _expected_event(2, "custom-index", "custom-sourcetype", "custom-source", "custom-host", raw);
  _assert_bytes_eq(written->str, written->len, expected->str, expected->len);

  /* the indexer confirms event id 2 */
  log_transport_mock_inject_data((LogTransportMock *) transport, "\xfb\x02", 2);
  cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
  cr_assert_eq(acks, 1);

  g_string_free(written, TRUE);
  g_string_free(expected, TRUE);
  log_msg_unref(msg);
  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, post_falls_back_to_default_metadata)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _client_new(transport);

  cr_assert(_run_handshake(client, 100));
  g_string_free(_written_bytes(transport), TRUE);

  LogMessage *msg = _test_msg_new();
  log_msg_set_value(msg, LM_V_HOST, "msg-host", -1);

  const gchar *raw = "fallback payload";
  gboolean consumed = FALSE;
  cr_assert_eq(log_proto_client_post(client, msg, (guchar *) g_strdup(raw), strlen(raw), &consumed), LPS_SUCCESS);
  cr_assert(consumed);

  GString *written = _written_bytes(transport);
  GString *expected = _expected_event(2, "main", "axosyslog", "axosyslog", "msg-host", raw);
  _assert_bytes_eq(written->str, written->len, expected->str, expected->len);

  g_string_free(written, TRUE);
  g_string_free(expected, TRUE);
  log_msg_unref(msg);
  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, post_is_not_consumed_while_handshake_is_pending)
{
  /* no reply from the indexer yet */
  LogTransport *transport = log_transport_mock_endless_stream_new(LTM_INJECT_ERROR(EAGAIN), LTM_EOF);
  LogProtoClient *client = _client_new(transport);

  LogMessage *msg = _test_msg_new();
  guchar *payload = (guchar *) g_strdup("queued payload");
  gboolean consumed = FALSE;

  cr_assert_eq(log_proto_client_post(client, msg, payload, strlen((gchar *) payload), &consumed), LPS_PARTIAL);
  cr_assert_not(consumed, "message must stay in the queue until the handshake finishes");
  cr_assert_eq(acks, 0);

  g_free(payload);
  log_msg_unref(msg);
  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, process_in_detects_eof_when_connected)
{
  /* non-endless mock: after the scripted reply, reads return EOF */
  LogTransport *transport = log_transport_mock_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _client_new(transport);

  cr_assert(_run_handshake(client, 100));
  cr_assert_eq(log_proto_client_process_in(client), LPS_EOF);

  log_proto_client_free(client);
}

static LogProtoClient *
_connected_client_with_posted_events(LogTransport *transport, gint n_events)
{
  LogProtoClient *client = _client_new(transport);

  cr_assert(_run_handshake(client, 100));

  for (gint i = 0; i < n_events; i++)
    {
      LogMessage *msg = _test_msg_new();
      gboolean consumed = FALSE;
      cr_assert_eq(log_proto_client_post(client, msg, (guchar *) g_strdup("payload"), 7, &consumed), LPS_SUCCESS);
      cr_assert(consumed);
      log_msg_unref(msg);
    }
  return client;
}

Test(splunk_s2s_proto_client, ack_range_confirms_all_covered_messages)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _connected_client_with_posted_events(transport, 3);

  /* 0xfa lo=2 hi=4: the indexer confirms event ids 2..4 in one range */
  log_transport_mock_inject_data((LogTransportMock *) transport, "\xfa\x02\x04", 3);
  cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
  cr_assert_eq(acks, 3);
  cr_assert_eq(rewinds, 0);

  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, out_of_order_acks_are_reported_contiguously)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _connected_client_with_posted_events(transport, 3);

  /* id 3 confirmed first: nothing can be acked, the writer backlog is FIFO */
  log_transport_mock_inject_data((LogTransportMock *) transport, "\xfb\x03", 2);
  cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
  cr_assert_eq(acks, 0);

  /* id 2 fills the gap: ids 2-3 are confirmed now */
  log_transport_mock_inject_data((LogTransportMock *) transport, "\xfb\x02", 2);
  cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
  cr_assert_eq(acks, 2);

  log_transport_mock_inject_data((LogTransportMock *) transport, "\xfb\x04", 2);
  cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
  cr_assert_eq(acks, 3);

  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, ack_of_never_sent_id_fails_loud)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _connected_client_with_posted_events(transport, 1);

  log_transport_mock_inject_data((LogTransportMock *) transport, "\xfb\x09", 2);
  cr_assert_eq(log_proto_client_process_in(client), LPS_ERROR);

  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, unknown_reverse_packet_fails_loud)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _connected_client_with_posted_events(transport, 1);

  log_transport_mock_inject_data((LogTransportMock *) transport, "\xf0\x02", 2);
  cr_assert_eq(log_proto_client_process_in(client), LPS_ERROR);

  log_proto_client_free(client);
}

Test(splunk_s2s_proto_client, connection_loss_with_unconfirmed_messages_rewinds)
{
  /* non-endless mock: reads hit EOF once the reply is consumed */
  LogTransport *transport = log_transport_mock_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _connected_client_with_posted_events(transport, 2);

  cr_assert_eq(log_proto_client_process_in(client), LPS_EOF);
  cr_assert_eq(rewinds, 1);
  cr_assert_eq(acks, 0);

  /* the proto is torn down after the failure, no double rewind */
  log_proto_client_free(client);
  cr_assert_eq(rewinds, 1);
}

Test(splunk_s2s_proto_client, freeing_with_unconfirmed_messages_rewinds)
{
  LogTransport *transport = log_transport_mock_endless_stream_new(SERVER_HELLO, SERVER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _connected_client_with_posted_events(transport, 2);

  /* id 2 is confirmed, id 3 is still in flight when the proto goes away */
  log_transport_mock_inject_data((LogTransportMock *) transport, "\xfb\x02", 2);
  cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
  cr_assert_eq(acks, 1);

  log_proto_client_free(client);
  cr_assert_eq(rewinds, 1);
}

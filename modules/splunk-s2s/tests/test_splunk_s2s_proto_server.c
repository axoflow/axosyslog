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
#include "libtest/proto_lib.h"
#include "libtest/grab-logging.h"

#include "splunk-s2s-proto-server.h"
#include "splunk-s2s-protocol.h"

#include "apphook.h"
#include "cfg.h"

#include <string.h>

#define AGGREGATED_FLAGS SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER
/* what universal forwarders send on raw file chunks, the aggregated bit clear */
#define CHUNK_FLAGS 0x3f

static LogProtoServer *
_server_new(LogTransport *transport)
{
  return log_proto_splunk_s2s_server_new(transport, get_inited_proto_server_options());
}

static void
_append_signature(GString *stream, const gchar *capabilities)
{
  splunk_s2s_format_header1(stream, "forwarder", "8089");
  splunk_s2s_format_v3_signature_frame(stream, capabilities);
}

static void
_append_forwarder_info(GString *stream)
{
  splunk_s2s_format_v3_forwarder_info_frame(stream, "ForwarderInfo build=test version=10.2.2", "test-guid", 1700000000);
}

static void
_append_event(GString *stream, guint64 channel_id, guint64 flags, guint64 event_id, const gchar *raw)
{
  splunk_s2s_format_event(stream, channel_id, flags, 1700000000, event_id, NULL, 0, raw, strlen(raw));
}

/* the codec only has a formatter for OPEN_CHANNEL, format the clone by hand */
static void
_append_clone_channel(GString *stream, guint64 template_channel_id, guint64 channel_id,
                      const gchar *source, const gchar *host, const gchar *sourcetype)
{
  g_string_append_c(stream, (gchar) SPLUNK_S2S_PKT_CLONE_CHANNEL);
  splunk_s2s_write_varint(stream, template_channel_id);
  splunk_s2s_write_varint(stream, channel_id);

  const gchar *headers[] = { "source::", source, "host::", host, "sourcetype::", sourcetype, "", "CLONE_CHANNEL" };
  for (gsize i = 0; i < G_N_ELEMENTS(headers); i += 2)
    {
      if (!headers[i + 1])
        {
          splunk_s2s_write_varint(stream, 0);
          continue;
        }
      splunk_s2s_write_varint(stream, strlen(headers[i]) + strlen(headers[i + 1]) + 1);
      g_string_append(stream, headers[i]);
      g_string_append(stream, headers[i + 1]);
    }
  splunk_s2s_write_varint(stream, 0);
}

static GString *
_expected_hello(void)
{
  GString *expected = g_string_new(NULL);

  splunk_s2s_format_v3_control_frame(expected, SPLUNK_S2S_CAPABILITIES_SERVER_HELLO);
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

typedef struct _FetchedMessage
{
  GString *raw;
  gchar *index;
  gchar *source;
  gchar *sourcetype;
  gchar *host;
  gchar *host_macro;
  guint64 timestamp;
} FetchedMessage;

static void
_fetched_message_clear(FetchedMessage *message)
{
  if (message->raw)
    g_string_free(message->raw, TRUE);
  g_free(message->index);
  g_free(message->source);
  g_free(message->sourcetype);
  g_free(message->host);
  g_free(message->host_macro);
  memset(message, 0, sizeof(*message));
}

static void
_collect_aux_nv_pair(const gchar *name, const gchar *value, gsize value_len, gpointer user_data)
{
  FetchedMessage *message = user_data;

  if (strcmp(name, ".splunk.index") == 0)
    message->index = g_strdup(value);
  else if (strcmp(name, ".splunk.source") == 0)
    message->source = g_strdup(value);
  else if (strcmp(name, ".splunk.sourcetype") == 0)
    message->sourcetype = g_strdup(value);
  else if (strcmp(name, ".splunk.host") == 0)
    message->host = g_strdup(value);
  else if (strcmp(name, "HOST") == 0)
    message->host_macro = g_strdup(value);
}

static LogProtoStatus
_fetch(LogProtoServer *proto, FetchedMessage *message)
{
  Bookmark bookmark;
  LogTransportAuxData aux;
  gboolean may_read = TRUE;
  const guchar *msg = NULL;
  gsize msg_len = 0;
  LogProtoStatus status;

  memset(message, 0, sizeof(*message));
  log_transport_aux_data_init(&aux);
  do
    {
      log_transport_aux_data_reinit(&aux);
      status = log_proto_server_fetch(proto, &msg, &msg_len, &may_read, &aux, &bookmark);
    }
  while (status == LPS_SUCCESS && msg == NULL);

  if (status == LPS_SUCCESS && msg)
    {
      message->raw = g_string_new_len((const gchar *) msg, msg_len);
      message->timestamp = aux.timestamp.tv_sec;
      log_transport_aux_data_foreach(&aux, _collect_aux_nv_pair, message);
    }
  log_transport_aux_data_destroy(&aux);
  return status;
}

static void
_assert_fetch(LogProtoServer *proto, FetchedMessage *message, const gchar *expected_raw)
{
  cr_assert_eq(_fetch(proto, message), LPS_SUCCESS);
  cr_assert_not_null(message->raw, "expected a message but none was returned: %s", expected_raw);
  cr_assert_eq(message->raw->len, strlen(expected_raw),
               "message length mismatch, actual: '%.*s' expected: '%s'",
               (gint) message->raw->len, message->raw->str, expected_raw);
  cr_assert_arr_eq(message->raw->str, expected_raw, message->raw->len);
}

static void
_assert_no_more_messages(LogProtoServer *proto)
{
  FetchedMessage message;

  cr_assert_eq(_fetch(proto, &message), LPS_EOF);
  cr_assert_null(message.raw);
}

static void
_assert_fetch_fails(LogProtoServer *proto)
{
  FetchedMessage message;

  start_grabbing_messages();
  cr_assert_eq(_fetch(proto, &message), LPS_ERROR);
  stop_grabbing_messages();
  cr_assert_null(message.raw);
}

Test(splunk_s2s_proto_server, handshake_replies_with_the_capability_grant)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);

  /* stress the incremental parser with one-byte reads */
  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  _assert_no_more_messages(proto);

  GString *written = _written_bytes(transport);
  GString *expected = _expected_hello();
  cr_assert_eq(written->len, expected->len);
  cr_assert_arr_eq(written->str, expected->str, expected->len);

  _assert_no_more_messages(proto);

  g_string_free(expected, TRUE);
  g_string_free(written, TRUE);
  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, forwarder_info_is_delivered_as_an_internal_message)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  cr_assert_str_eq(message.index, "_internal");
  cr_assert_str_eq(message.source, "fwd");
  cr_assert_str_eq(message.sourcetype, "fwdinfo");
  cr_assert_str_eq(message.host, "$decideOnStartup");
  cr_assert_str_eq(message.host_macro, "$decideOnStartup");
  cr_assert_eq(message.timestamp, 1700000000);
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, aggregated_events_map_to_one_message_each)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, 7, "/var/log/app.log", "webserver", "app_logs");
  _append_event(stream, 7, AGGREGATED_FLAGS, 2, "first event\nwith an embedded line break");
  SplunkS2SEventField fields[] =
  {
    { .name = "_MetaData:Index", .value_type = SPLUNK_S2S_VALUE_STR, .str_value = "custom", .str_value_len = 6 },
    { .name = "MetaData:Sourcetype", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = "sourcetype::override_st", .str_value_len = 23 },
    { .name = "MetaData:Host", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = "host::other-host", .str_value_len = 16 },
  };
  splunk_s2s_format_event(stream, 7, AGGREGATED_FLAGS, 1782963360, 3, fields, G_N_ELEMENTS(fields),
                          "second event", 12);

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);

  /* channel defaults apply when the event carries no overrides */
  _assert_fetch(proto, &message, "first event\nwith an embedded line break");
  cr_assert_null(message.index);
  cr_assert_str_eq(message.source, "/var/log/app.log");
  cr_assert_str_eq(message.sourcetype, "app_logs");
  cr_assert_str_eq(message.host, "webserver");
  cr_assert_str_eq(message.host_macro, "webserver");
  cr_assert_eq(message.timestamp, 1700000000);
  _fetched_message_clear(&message);

  /* per-event fields override the channel defaults */
  _assert_fetch(proto, &message, "second event");
  cr_assert_str_eq(message.index, "custom");
  cr_assert_str_eq(message.source, "/var/log/app.log");
  cr_assert_str_eq(message.sourcetype, "override_st");
  cr_assert_str_eq(message.host, "other-host");
  cr_assert_eq(message.timestamp, 1782963360);
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, raw_chunks_are_reassembled_and_split_on_line_breaks)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, 1, "/corpus/access.log", "forwarder", "access_combined");

  /* a line spans the two chunks, CRLF line ends are consumed and empty
   * lines are merged away; the unterminated tail is flushed on EOF */
  _append_event(stream, 1, CHUNK_FLAGS, 2, "line1\r\n\r\nli");
  _append_event(stream, 1, CHUNK_FLAGS, 3, "ne2\nline3");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);

  _assert_fetch(proto, &message, "line1");
  cr_assert_str_eq(message.source, "/corpus/access.log");
  cr_assert_str_eq(message.sourcetype, "access_combined");
  cr_assert_str_eq(message.host, "forwarder");
  _fetched_message_clear(&message);

  _assert_fetch(proto, &message, "line2");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "line3");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, close_channel_flushes_the_chunk_remainder)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, 1, "/corpus/a.log", "forwarder", "st");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "complete\nunterminated tail");
  splunk_s2s_format_close_channel(stream, 1);
  _append_event(stream, 1, AGGREGATED_FLAGS, 3, "the channel is gone");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "complete");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "unterminated tail");
  _fetched_message_clear(&message);

  /* events on a closed channel fail loud */
  start_grabbing_messages();
  cr_assert_eq(_fetch(proto, &message), LPS_ERROR);
  stop_grabbing_messages();
  assert_grabbed_log_contains("splunk-s2s: event received on a channel that was never opened");

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, clone_channel_opens_the_second_channel_id)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, 1, "/corpus/a.log", "forwarder", "st");
  _append_clone_channel(stream, 1, 2, "/var/log/introspection/resource_usage.log", "forwarder", "splunk_resource_usage");
  _append_event(stream, 2, AGGREGATED_FLAGS, 2, "cloned channel event");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);

  _assert_fetch(proto, &message, "cloned channel event");
  cr_assert_str_eq(message.source, "/var/log/introspection/resource_usage.log");
  cr_assert_str_eq(message.sourcetype, "splunk_resource_usage");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, clone_channel_with_an_absent_slot)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, 1, "/corpus/a.log", "forwarder", "st");
  /* an absent host slot must not terminate the slot list early */
  _append_clone_channel(stream, 1, 2, "/var/log/introspection/resource_usage.log", NULL, "splunk_resource_usage");
  _append_event(stream, 2, AGGREGATED_FLAGS, 2, "cloned channel event");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);

  _assert_fetch(proto, &message, "cloned channel event");
  cr_assert_str_eq(message.source, "/var/log/introspection/resource_usage.log");
  cr_assert_str_eq(message.sourcetype, "splunk_resource_usage");
  cr_assert_null(message.host);
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, legacy_greeting_is_accepted)
{
  GString *stream = g_string_new(NULL);
  g_string_set_size(stream, SPLUNK_S2S_HEADER1_SIZE);
  memset(stream->str, 0, SPLUNK_S2S_HEADER1_SIZE);
  memcpy(stream->str, SPLUNK_S2S_MAGIC_V2, strlen(SPLUNK_S2S_MAGIC_V2));
  memcpy(stream->str + 0x80, "legacy-forwarder", 16);
  splunk_s2s_format_v3_signature_frame(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  _assert_no_more_messages(proto);

  GString *written = _written_bytes(transport);
  GString *expected = _expected_hello();
  cr_assert_eq(written->len, expected->len);
  cr_assert_arr_eq(written->str, expected->str, expected->len);

  g_string_free(expected, TRUE);
  g_string_free(written, TRUE);
  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, compressed_handshake_fails_loud)
{
  GString *stream = g_string_new(NULL);
  splunk_s2s_format_header1(stream, "forwarder", "8089");
  /* a compressed connection's first handshake block: BE-u32 length, then a
   * zlib stream in place of the plaintext v3 frame body */
  g_string_append_len(stream, "\x00\x00\x00\x2a\x78\x9c\x63\x60", 8);

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  _assert_fetch_fails(proto);
  assert_grabbed_log_contains("splunk-s2s: the forwarder compresses the stream");

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, overlong_varint_fails_loud)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  /* an event whose channel id varint never terminates */
  g_string_append_c(stream, (gchar) SPLUNK_S2S_PKT_EVENT);
  for (gint i = 0; i < 11; i++)
    g_string_append_c(stream, '\x80');

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);

  start_grabbing_messages();
  cr_assert_eq(_fetch(proto, &message), LPS_ERROR);
  stop_grabbing_messages();
  assert_grabbed_log_contains("invalid varint running past the 64-bit ceiling");

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

/* 0x00 is not a packet id: historic "introspection" sightings were
 * channel-header mis-frames, treat it as a desync like any unknown id */
Test(splunk_s2s_proto_server, zero_packet_id_is_a_desync)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  g_string_append_c(stream, '\x00');

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);

  start_grabbing_messages();
  cr_assert_eq(_fetch(proto, &message), LPS_ERROR);
  stop_grabbing_messages();
  assert_grabbed_log_contains("splunk-s2s: unexpected packet");

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, decoded_event_ids_are_acknowledged_when_requested)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE_ACK);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, 1, "/corpus/a.log", "forwarder", "st");
  _append_event(stream, 1, AGGREGATED_FLAGS, 2, "one");
  _append_event(stream, 1, AGGREGATED_FLAGS, 3, "two");
  _append_event(stream, 1, AGGREGATED_FLAGS, 4, "three");
  /* batch markers produce no message but must be acknowledged */
  _append_event(stream, 1, SPLUNK_S2S_EVENT_FLAGS_SHORT_HEADER, 5, "");
  /* an id gap breaks the contiguous run */
  _append_event(stream, 1, AGGREGATED_FLAGS, 7, "four");

  /* acknowledgements coalesce within a decode round, deliver the whole
   * stream in one read to make the coalescing deterministic */
  LogTransport *transport = log_transport_mock_records_new(stream->str, (gint) stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  const gchar *expected_messages[] = { "ForwarderInfo build=test version=10.2.2", "one", "two", "three", "four" };
  for (gsize i = 0; i < G_N_ELEMENTS(expected_messages); i++)
    {
      _assert_fetch(proto, &message, expected_messages[i]);
      _fetched_message_clear(&message);
    }
  _assert_no_more_messages(proto);

  GString *expected = _expected_hello();
  splunk_s2s_format_ack(expected, 2, 5);
  splunk_s2s_format_ack(expected, 7, 7);

  GString *written = _written_bytes(transport);
  cr_assert_eq(written->len, expected->len);
  cr_assert_arr_eq(written->str, expected->str, expected->len);

  g_string_free(expected, TRUE);
  g_string_free(written, TRUE);
  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, no_acknowledgements_without_the_capability)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, 1, "/corpus/a.log", "forwarder", "st");
  _append_event(stream, 1, AGGREGATED_FLAGS, 2, "one");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "one");
  _fetched_message_clear(&message);
  _assert_no_more_messages(proto);

  GString *expected = _expected_hello();
  GString *written = _written_bytes(transport);
  cr_assert_eq(written->len, expected->len);
  cr_assert_arr_eq(written->str, expected->str, expected->len);

  g_string_free(expected, TRUE);
  g_string_free(written, TRUE);
  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, oversized_event_fails_loud)
{
  proto_server_options.max_msg_size = 16;

  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  start_grabbing_messages();
  cr_assert_eq(_fetch(proto, &message), LPS_ERROR);
  stop_grabbing_messages();
  assert_grabbed_log_contains("splunk-s2s: incoming event larger than log-msg-size()");

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, oversized_event_is_trimmed_when_configured)
{
  proto_server_options.max_msg_size = 16;
  proto_server_options.trim_large_messages = TRUE;

  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, 1, "/corpus/a.log", "forwarder", "st");
  _append_event(stream, 1, AGGREGATED_FLAGS, 2, "this event is longer than sixteen bytes");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo bu");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "this event is lo");
  _fetched_message_clear(&message);
  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, compression_request_fails_loud)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, "ack=0;compression=1");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  _assert_fetch_fails(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, zlib_switch_fails_loud)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  g_string_append_c(stream, (gchar) SPLUNK_S2S_PKT_START_ZLIB);

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);

  start_grabbing_messages();
  cr_assert_eq(_fetch(proto, &message), LPS_ERROR);
  stop_grabbing_messages();
  assert_grabbed_log_contains("splunk-s2s: the forwarder switched to stream compression");

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, unknown_packet_fails_loud)
{
  GString *stream = g_string_new(NULL);
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  g_string_append_c(stream, '\x42');

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  FetchedMessage message;
  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);

  start_grabbing_messages();
  cr_assert_eq(_fetch(proto, &message), LPS_ERROR);
  stop_grabbing_messages();
  assert_grabbed_log_contains("splunk-s2s: unexpected packet");

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, invalid_greeting_fails_loud)
{
  gchar bogus[SPLUNK_S2S_HEADER1_SIZE] = "definitely not a splunk forwarder";

  LogTransport *transport = log_transport_mock_stream_new(bogus, sizeof(bogus), LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  _assert_fetch_fails(proto);

  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, eof_during_the_handshake)
{
  GString *stream = g_string_new(NULL);
  splunk_s2s_format_header1(stream, "forwarder", "8089");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new(transport);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

static void
setup(void)
{
  app_startup();
  init_proto_tests();
}

static void
teardown(void)
{
  deinit_proto_tests();
  app_shutdown();
}

TestSuite(splunk_s2s_proto_server, .init = setup, .fini = teardown);

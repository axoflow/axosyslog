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
#include "reloc.h"

#include <string.h>
#include <iv.h>
#include <unistd.h>

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
    {
      .name = "MetaData:Sourcetype", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = "sourcetype::override_st", .str_value_len = 23
    },
    {
      .name = "MetaData:Host", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = "host::other-host", .str_value_len = 16
    },
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

/****************************************************************************
 * Multi-line events
 ****************************************************************************/

static MultiLineOptions multi_line_options;
static gboolean multi_line_options_set;

static LogProtoServer *
_server_new_multi_line(LogTransport *transport, const gchar *mode, const gchar *prefix, const gchar *garbage,
                       gint timeout_msec)
{
  GError *error = NULL;

  if (multi_line_options_set)
    multi_line_options_destroy(&multi_line_options);
  multi_line_options_defaults(&multi_line_options);
  multi_line_options_set = TRUE;

  cr_assert(multi_line_options_set_mode(&multi_line_options, mode), "invalid multi-line mode %s", mode);
  if (prefix)
    cr_assert(multi_line_options_set_prefix(&multi_line_options, prefix, &error), "invalid prefix %s", prefix);
  if (garbage)
    cr_assert(multi_line_options_set_garbage(&multi_line_options, garbage, &error), "invalid garbage %s", garbage);
  cr_assert(multi_line_options_validate(&multi_line_options));

  LogProtoServer *proto = _server_new(transport);
  log_proto_splunk_s2s_server_set_multi_line(proto, &multi_line_options, timeout_msec);
  return proto;
}

/* the handshake up to the first channel, so the tests below read as the data */
static void
_append_preamble_with_channel(GString *stream, guint64 channel_id, const gchar *source)
{
  _append_signature(stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _append_forwarder_info(stream);
  splunk_s2s_format_open_channel(stream, channel_id, source, "forwarder", "java_app");
}

static void
_assert_forwarder_info(LogProtoServer *proto)
{
  FetchedMessage message;

  _assert_fetch(proto, &message, "ForwarderInfo build=test version=10.2.2");
  _fetched_message_clear(&message);
}

/* a single fetch(), for streams that never reach EOF */
static gboolean
_fetch_once(LogProtoServer *proto, FetchedMessage *message)
{
  Bookmark bookmark;
  LogTransportAuxData aux;
  gboolean may_read = TRUE;
  const guchar *msg = NULL;
  gsize msg_len = 0;

  memset(message, 0, sizeof(*message));
  log_transport_aux_data_init(&aux);
  LogProtoStatus status = log_proto_server_fetch(proto, &msg, &msg_len, &may_read, &aux, &bookmark);
  cr_assert_eq(status, LPS_SUCCESS);
  if (msg)
    {
      message->raw = g_string_new_len((const gchar *) msg, msg_len);
      log_transport_aux_data_foreach(&aux, _collect_aux_nv_pair, message);
    }
  log_transport_aux_data_destroy(&aux);
  return msg != NULL;
}

Test(splunk_s2s_proto_server, prefix_mode_merges_continuation_lines_across_chunks)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");

  /* the second event is cut in two by the chunk boundary, the third is only
   * complete at EOF; empty lines vanish as they do without merging */
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 15:31:50 ERROR failed\n\n"
                                           "com.example.Exception: boom\n\tat com.example.A(A.java:1)\n2026-10-09 15:31:51 INFO sec");
  _append_event(stream, 1, CHUNK_FLAGS, 3, "ond\n\tat com.example.B(B.java:2)\n2026-10-09 15:31:52 INFO third\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-[0-9]{2}-[0-9]{2} ", NULL, 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  _assert_fetch(proto, &message,
                "2026-10-09 15:31:50 ERROR failed\ncom.example.Exception: boom\n\tat com.example.A(A.java:1)");
  cr_assert_str_eq(message.source, "/var/log/app.log");
  cr_assert_str_eq(message.sourcetype, "java_app");
  cr_assert_str_eq(message.host, "forwarder");
  _fetched_message_clear(&message);

  _assert_fetch(proto, &message, "2026-10-09 15:31:51 INFO second\n\tat com.example.B(B.java:2)");
  _fetched_message_clear(&message);

  _assert_fetch(proto, &message, "2026-10-09 15:31:52 INFO third");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

/* every line of an event ends in a newline, so dropping the garbage line
 * leaves the newline of the line before it in place */
Test(splunk_s2s_proto_server, a_dropped_garbage_line_leaves_the_newline_of_the_line_before_it)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 first\n\tat A\n--END--\n2026-10-09 second\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", "--END--", 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  _assert_fetch(proto, &message, "2026-10-09 first\n\tat A\n");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "2026-10-09 second");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, indented_mode_merges_indented_continuation_lines)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "first\n  continued\n\tand again\nsecond\n  continued\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "indented", NULL, NULL, 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  _assert_fetch(proto, &message, "first\n  continued\n\tand again");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "second\n  continued");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, close_channel_completes_the_pending_multi_line_event)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  /* the unterminated tail is the last line of the event */
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 first\n\tat A\n\tat B");
  splunk_s2s_format_close_channel(stream, 1);
  splunk_s2s_format_open_channel(stream, 2, "/var/log/other.log", "forwarder", "java_app");
  _append_event(stream, 2, CHUNK_FLAGS, 3, "2026-10-09 other\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", NULL, 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  _assert_fetch(proto, &message, "2026-10-09 first\n\tat A\n\tat B");
  cr_assert_str_eq(message.source, "/var/log/app.log");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "2026-10-09 other");
  cr_assert_str_eq(message.source, "/var/log/other.log");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, channels_accumulate_independently)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/a.log");
  splunk_s2s_format_open_channel(stream, 2, "/var/log/b.log", "forwarder", "java_app");

  /* chunks of the two channels interleave on the connection */
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 a1\n\tat A\n");
  _append_event(stream, 2, CHUNK_FLAGS, 3, "2026-10-09 b1\n\tat B\n");
  _append_event(stream, 1, CHUNK_FLAGS, 4, "\tat A2\n2026-10-09 a2\n");
  _append_event(stream, 2, CHUNK_FLAGS, 5, "2026-10-09 b2\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", NULL, 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  _assert_fetch(proto, &message, "2026-10-09 a1\n\tat A\n\tat A2");
  cr_assert_str_eq(message.source, "/var/log/a.log");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "2026-10-09 b1\n\tat B");
  cr_assert_str_eq(message.source, "/var/log/b.log");
  _fetched_message_clear(&message);

  /* the pending last events are completed at EOF, in channel order */
  _assert_fetch(proto, &message, "2026-10-09 a2");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "2026-10-09 b2");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, a_metadata_change_on_the_channel_completes_the_pending_event)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 first\n\tat A\n");

  SplunkS2SEventField fields[] =
  {
    {
      .name = "MetaData:Source", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = "source::/var/log/other.log", .str_value_len = 26
    },
  };
  splunk_s2s_format_event(stream, 1, CHUNK_FLAGS, 1700000000, 3, fields, G_N_ELEMENTS(fields),
                          "\tat B\n", 6);

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", NULL, 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  _assert_fetch(proto, &message, "2026-10-09 first\n\tat A");
  cr_assert_str_eq(message.source, "/var/log/app.log");
  _fetched_message_clear(&message);

  /* the continuation line of another source starts an event of its own */
  _assert_fetch(proto, &message, "\tat B");
  cr_assert_str_eq(message.source, "/var/log/other.log");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, aggregated_events_bypass_the_accumulator)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 first\n\tat A\n");
  /* a heavy forwarder merged this one already, it does not join the pending event */
  _append_event(stream, 1, AGGREGATED_FLAGS, 3, "\tnot a continuation\n\tof anything");
  _append_event(stream, 1, CHUNK_FLAGS, 4, "\tat B\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", NULL, 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  _assert_fetch(proto, &message, "\tnot a continuation\n\tof anything");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "2026-10-09 first\n\tat A\n\tat B");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

/* iv_now is read once per loop iteration, and there is no loop here */
static void
_sleep_msec(gint msec)
{
  usleep(msec * 1000);
  iv_invalidate_now();
}

Test(splunk_s2s_proto_server, the_pending_event_is_completed_when_the_timeout_fires)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 first\n\tat A\n");

  /* no EOF: the stream goes quiet after the chunk; the records mock hands
   * the whole buffer to a single read, so one fetch() sees all of it */
  LogTransport *transport = log_transport_mock_endless_records_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", NULL, 100);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  cr_assert_not(_fetch_once(proto, &message), "the event must wait for its next line");

  /* the timer firing alone does not complete an event whose timeout is still running */
  log_proto_splunk_s2s_server_fire_multi_line_timeout(proto);
  cr_assert_not(_fetch_once(proto, &message), "the event must be held until its timeout passes");

  _sleep_msec(200);
  log_proto_splunk_s2s_server_fire_multi_line_timeout(proto);
  cr_assert(_fetch_once(proto, &message));
  cr_assert_str_eq(message.raw->str, "2026-10-09 first\n\tat A");
  cr_assert_str_eq(message.source, "/var/log/app.log");
  _fetched_message_clear(&message);

  cr_assert_not(_fetch_once(proto, &message));

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, the_timeout_expires_per_channel_counted_from_its_last_line)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/quiet.log");
  splunk_s2s_format_open_channel(stream, 2, "/var/log/busy.log", "forwarder", "java_app");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 quiet\n\tat A\n");
  _append_event(stream, 2, CHUNK_FLAGS, 3, "2026-10-09 busy\n\tat B\n");

  LogTransport *transport = log_transport_mock_endless_records_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", NULL, 100);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  cr_assert_not(_fetch_once(proto, &message));

  /* the busy channel gets another line just before the deadline */
  _sleep_msec(150);
  GString *next_chunk = g_string_new(NULL);
  _append_event(next_chunk, 2, CHUNK_FLAGS, 4, "\tat C\n");
  log_transport_mock_inject_data((LogTransportMock *) transport, next_chunk->str, next_chunk->len);
  cr_assert_not(_fetch_once(proto, &message));

  /* only the quiet channel has expired */
  log_proto_splunk_s2s_server_fire_multi_line_timeout(proto);
  cr_assert(_fetch_once(proto, &message));
  cr_assert_str_eq(message.raw->str, "2026-10-09 quiet\n\tat A");
  cr_assert_str_eq(message.source, "/var/log/quiet.log");
  _fetched_message_clear(&message);
  cr_assert_not(_fetch_once(proto, &message), "the busy channel's event must wait for its own timeout");

  _sleep_msec(150);
  log_proto_splunk_s2s_server_fire_multi_line_timeout(proto);
  cr_assert(_fetch_once(proto, &message));
  cr_assert_str_eq(message.raw->str, "2026-10-09 busy\n\tat B\n\tat C");
  cr_assert_str_eq(message.source, "/var/log/busy.log");
  _fetched_message_clear(&message);

  g_string_free(next_chunk, TRUE);
  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, without_a_timeout_the_pending_event_waits_for_the_next_line)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 first\n\tat A\n");

  LogTransport *transport = log_transport_mock_endless_records_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", NULL, 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  cr_assert_not(_fetch_once(proto, &message));
  log_proto_splunk_s2s_server_fire_multi_line_timeout(proto);
  cr_assert_not(_fetch_once(proto, &message));

  /* the next chunk arrives: its first line completes the pending event */
  GString *next_chunk = g_string_new(NULL);
  _append_event(next_chunk, 1, CHUNK_FLAGS, 3, "2026-10-09 second\n");
  log_transport_mock_inject_data((LogTransportMock *) transport, next_chunk->str, next_chunk->len);
  cr_assert(_fetch_once(proto, &message));
  cr_assert_str_eq(message.raw->str, "2026-10-09 first\n\tat A");
  _fetched_message_clear(&message);

  g_string_free(next_chunk, TRUE);
  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, merging_stops_before_the_event_would_exceed_log_msg_size)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 first\n"
                                           "\tat com.example.A(A.java:1)\n\tat com.example.B(B.java:2)\n\tat com.example.C(C.java:3)\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "prefix-garbage", "^[0-9]{4}-", NULL, 0);
  /* the proto reads the options through the pointer it was handed; the
   * limit still fits the forwarder info message of the handshake */
  proto_server_options.max_msg_size = 60;

  _assert_forwarder_info(proto);

  FetchedMessage message;
  /* 16 + 1 + 27 = 44 bytes, the next frame would make it 72 */
  _assert_fetch(proto, &message, "2026-10-09 first\n\tat com.example.A(A.java:1)");
  _fetched_message_clear(&message);
  /* 27 + 1 + 27 = 55 bytes */
  _assert_fetch(proto, &message, "\tat com.example.B(B.java:2)\n\tat com.example.C(C.java:3)");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

/* the smart mode keeps state across lines, which the cap cuts through */
Test(splunk_s2s_proto_server, the_size_cap_cuts_through_a_trace_of_the_smart_mode)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2,
                "Traceback (most recent call last):\n"
                "File \"./lib/merge-grammar.py\", line 62, in <module>\n"
                "  for line in fileinput.input(openhook=fileinput.hook_encoded(\"utf-8\")):\n"
                "File \"/usr/lib/python3.8/fileinput.py\", line 248, in __next__\n"
                "  line = self._readline()\n"
                "unrelated line here\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "smart", NULL, NULL, 0);
  proto_server_options.max_msg_size = 100;

  _assert_forwarder_info(proto);

  FetchedMessage message;
  /* 34 + 1 + 51 bytes, the third line would make it 161 */
  _assert_fetch(proto, &message, "Traceback (most recent call last):\n"
                                 "File \"./lib/merge-grammar.py\", line 62, in <module>");
  _fetched_message_clear(&message);
  /* 74 bytes, the next line would make it 136 */
  _assert_fetch(proto, &message, "  for line in fileinput.input(openhook=fileinput.hook_encoded(\"utf-8\")):");
  _fetched_message_clear(&message);
  /* the trace goes on from where the cap cut it, and the unrelated line ends it */
  _assert_fetch(proto, &message, "File \"/usr/lib/python3.8/fileinput.py\", line 248, in __next__\n"
                                 "  line = self._readline()");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "unrelated line here");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

Test(splunk_s2s_proto_server, mode_none_leaves_lines_unmerged)
{
  GString *stream = g_string_new(NULL);
  _append_preamble_with_channel(stream, 1, "/var/log/app.log");
  _append_event(stream, 1, CHUNK_FLAGS, 2, "2026-10-09 first\n\tat A\n");

  LogTransport *transport = log_transport_mock_stream_new(stream->str, stream->len, LTM_EOF);
  LogProtoServer *proto = _server_new_multi_line(transport, "none", NULL, NULL, 0);

  _assert_forwarder_info(proto);

  FetchedMessage message;
  _assert_fetch(proto, &message, "2026-10-09 first");
  _fetched_message_clear(&message);
  _assert_fetch(proto, &message, "\tat A");
  _fetched_message_clear(&message);

  _assert_no_more_messages(proto);

  g_string_free(stream, TRUE);
  log_proto_server_free(proto);
}

static void
setup(void)
{
  /* the smart mode needs its state machine, which is not installed yet when
   * the tests run from the build tree */
  override_installation_path_for("${pkgdatadir}/smart-multi-line.fsm", TOP_SRCDIR "/lib/multi-line/smart-multi-line.fsm");
  override_installation_path_for("${pkgdatadir}/timestamp-multi-line.formats",
                                 TOP_SRCDIR "/lib/multi-line/timestamp-multi-line.formats");
  app_startup();
  init_proto_tests();
}

static void
teardown(void)
{
  if (multi_line_options_set)
    {
      multi_line_options_destroy(&multi_line_options);
      multi_line_options_set = FALSE;
    }
  deinit_proto_tests();
  app_shutdown();
}

TestSuite(splunk_s2s_proto_server, .init = setup, .fini = teardown);

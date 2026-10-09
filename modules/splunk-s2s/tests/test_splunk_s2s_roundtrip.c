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

/* these tests cannot catch both sides drifting the same way */

#include <criterion/criterion.h>

#include "libtest/mock-transport.h"
#include "libtest/proto_lib.h"

#include "splunk-s2s-proto-client.h"
#include "splunk-s2s-proto-server.h"
#include "splunk-s2s-protocol.h"

#include "apphook.h"
#include "logmsg/logmsg.h"
#include "scratch-buffers.h"
#include "cfg.h"

#include <string.h>

/* an arbitrary v3 frame standing in for the indexer's reply; the client only
 * needs it to leave the handshake, it never inspects the contents */
#define SCRIPTED_INDEXER_HELLO "\x00\x00\x00\x05HELLO"
#define SCRIPTED_INDEXER_HELLO_LEN 9

#define MAX_MSG_SIZE (2 * 1024 * 1024)

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

static LogProtoClientOptionsStorage client_options;

static LogProtoClient *
_client_new(LogTransport *transport)
{
  LogProtoClient *client = log_proto_splunk_s2s_client_new(transport, &client_options.super);
  LogProtoClientFlowControlFuncs flow_control_funcs =
  {
    .ack_callback = _ack,
    .rewind_callback = _rewind,
  };

  log_proto_client_set_client_flow_control(client, &flow_control_funcs);
  return client;
}

static LogProtoServer *
_server_new(LogTransport *transport)
{
  proto_server_options.max_msg_size = MAX_MSG_SIZE;
  return log_proto_splunk_s2s_server_new(transport, get_inited_proto_server_options());
}

static LogMessage *
_msg_new(const gchar *index, const gchar *source, const gchar *sourcetype, const gchar *host, guint64 stamp)
{
  LogMessage *msg = log_msg_new_empty();

  msg->timestamps[LM_TS_STAMP].ut_sec = stamp;
  if (index)
    log_msg_set_value_by_name(msg, ".splunk.index", index, -1);
  if (source)
    log_msg_set_value_by_name(msg, ".splunk.source", source, -1);
  if (sourcetype)
    log_msg_set_value_by_name(msg, ".splunk.sourcetype", sourcetype, -1);
  if (host)
    log_msg_set_value_by_name(msg, ".splunk.host", host, -1);
  return msg;
}

typedef struct _DecodedMessage
{
  GString *raw;
  gchar *index;
  gchar *source;
  gchar *sourcetype;
  gchar *host;
  guint64 timestamp;
} DecodedMessage;

static void
_decoded_message_free(gpointer p)
{
  DecodedMessage *message = p;

  if (message->raw)
    g_string_free(message->raw, TRUE);
  g_free(message->index);
  g_free(message->source);
  g_free(message->sourcetype);
  g_free(message->host);
  g_free(message);
}

static void
_collect_aux_nv_pair(const gchar *name, const gchar *value, gsize value_len, gpointer user_data)
{
  DecodedMessage *message = user_data;

  if (strcmp(name, ".splunk.index") == 0)
    message->index = g_strdup(value);
  else if (strcmp(name, ".splunk.source") == 0)
    message->source = g_strdup(value);
  else if (strcmp(name, ".splunk.sourcetype") == 0)
    message->sourcetype = g_strdup(value);
  else if (strcmp(name, ".splunk.host") == 0)
    message->host = g_strdup(value);
}

/* one fetch attempt; returns a freshly allocated message, or NULL when the
 * proto had nothing to hand out on this call (EAGAIN / no progress) */
static DecodedMessage *
_fetch_once(LogProtoServer *proto, LogProtoStatus *status)
{
  LogTransportAuxData aux;
  Bookmark bookmark;
  gboolean may_read = TRUE;
  const guchar *msg = NULL;
  gsize msg_len = 0;

  log_transport_aux_data_init(&aux);
  *status = log_proto_server_fetch(proto, &msg, &msg_len, &may_read, &aux, &bookmark);

  DecodedMessage *decoded = NULL;
  if (*status == LPS_SUCCESS && msg)
    {
      decoded = g_new0(DecodedMessage, 1);
      decoded->raw = g_string_new_len((const gchar *) msg, msg_len);
      decoded->timestamp = aux.timestamp.tv_sec;
      log_transport_aux_data_foreach(&aux, _collect_aux_nv_pair, decoded);
    }
  log_transport_aux_data_destroy(&aux);
  return decoded;
}

/* collect up to expected_count messages, tolerating the odd empty fetch a
 * non-blocking transport returns between messages */
static GPtrArray *
_collect_messages(LogProtoServer *proto, guint expected_count)
{
  GPtrArray *messages = g_ptr_array_new_with_free_func(_decoded_message_free);
  gint idle = 0;

  while (messages->len < expected_count && idle < 16)
    {
      LogProtoStatus status;
      DecodedMessage *decoded = _fetch_once(proto, &status);

      if (decoded)
        {
          g_ptr_array_add(messages, decoded);
          idle = 0;
          continue;
        }
      if (status != LPS_SUCCESS && status != LPS_AGAIN)
        break;
      idle++;
    }
  return messages;
}

static void
_assert_metadata(const DecodedMessage *message, const gchar *index, const gchar *source,
                 const gchar *sourcetype, const gchar *host)
{
  cr_assert_str_eq(message->index, index, "index mismatch");
  cr_assert_str_eq(message->source, source, "source mismatch");
  cr_assert_str_eq(message->sourcetype, sourcetype, "sourcetype mismatch");
  cr_assert_str_eq(message->host, host, "host mismatch");
}

static void
setup(void)
{
  app_startup();
  init_proto_tests();
  log_proto_client_options_defaults(&client_options.super);
  acks = 0;
  rewinds = 0;
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_proto_tests();
  app_shutdown();
}

/* the proto stops asking for handshake I/O once it is connected and idle */
static gboolean
_client_handshake_done(LogProtoClient *client)
{
  GIOCondition cond, idle_cond;
  gint timeout = -1;

  return !log_proto_client_poll_prepare(client, &cond, &idle_cond, &timeout);
}

static gboolean
_run_client_handshake(LogProtoClient *client)
{
  for (gint i = 0; i < 100 && !_client_handshake_done(client); i++)
    {
      cr_assert_eq(log_proto_client_flush(client), LPS_SUCCESS);
      if (_client_handshake_done(client))
        break;
      cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
    }
  return _client_handshake_done(client);
}

TestSuite(splunk_s2s_roundtrip, .init = setup, .fini = teardown);

Test(splunk_s2s_roundtrip, codec_preserves_events_across_the_metadata_and_size_matrix)
{
  LogTransport *client_transport = log_transport_mock_endless_stream_new(SCRIPTED_INDEXER_HELLO,
                                   SCRIPTED_INDEXER_HELLO_LEN, LTM_EOF);
  LogProtoClient *client = _client_new(client_transport);

  cr_assert(_run_client_handshake(client), "client handshake did not finish");

  const gchar *host_name = g_get_host_name();

  /* a >64 kB payload forces a single aggregated event past the raw-chunk
   * boundary; embedded newlines must survive because aggregation is done */
  GString *big = g_string_new(NULL);
  for (gint i = 0; i < 5000; i++)
    g_string_append_printf(big, "line %d of a large aggregated event\n", i);
  cr_assert_gt(big->len, 64 * 1024);

  struct
  {
    const gchar *index;
    const gchar *source;
    const gchar *sourcetype;
    const gchar *host;
    const gchar *payload;
  } cases[] =
  {
    { "custom-index", "custom-source", "custom-st", "custom-host", "a fully specified event" },
    { NULL, NULL, NULL, NULL, "all metadata defaulted" },
    { "idx", NULL, "st", NULL, "partially specified metadata" },
    { "unicode-idx", "árvíztűrő", "sürüs", "tükörfúrógép", "utf-8 metadata and payload: 日本語" },
    { "multi", "src", "st", "h", "embedded\nnewlines\nstay\none\nmessage" },
    { "big", "src", "st", "h", big->str },
  };

  for (gsize i = 0; i < G_N_ELEMENTS(cases); i++)
    {
      LogMessage *msg = _msg_new(cases[i].index, cases[i].source, cases[i].sourcetype, cases[i].host, 1700000000 + i);
      gboolean consumed = FALSE;
      cr_assert_eq(log_proto_client_post(client, msg, (guchar *) g_strdup(cases[i].payload),
                                         strlen(cases[i].payload), &consumed), LPS_SUCCESS);
      cr_assert(consumed);
      log_msg_unref(msg);
    }

  GString *wire = g_string_sized_new(big->len + 4096);
  gchar buf[4096];
  gssize rc;
  while ((rc = log_transport_mock_read_from_write_buffer((LogTransportMock *) client_transport, buf, sizeof(buf))) > 0)
    g_string_append_len(wire, buf, rc);
  log_proto_client_free(client);

  LogTransport *server_transport = log_transport_mock_records_new(wire->str, (gint) wire->len, LTM_EOF);
  LogProtoServer *server = _server_new(server_transport);

  /* the client announces itself with a forwarder-info frame during the
   * handshake, which the server surfaces as the first (internal) message */
  guint expected = 1 + G_N_ELEMENTS(cases);
  GPtrArray *messages = _collect_messages(server, expected);
  cr_assert_eq(messages->len, expected, "expected %u messages, got %u", expected, messages->len);

  DecodedMessage *info = g_ptr_array_index(messages, 0);
  cr_assert_str_eq(info->index, "_internal");

  for (gsize i = 0; i < G_N_ELEMENTS(cases); i++)
    {
      DecodedMessage *message = g_ptr_array_index(messages, i + 1);

      cr_assert_eq(message->raw->len, strlen(cases[i].payload), "payload length mismatch for case %zu", i);
      cr_assert_arr_eq(message->raw->str, cases[i].payload, message->raw->len);

      _assert_metadata(message,
                       cases[i].index ? cases[i].index : "main",
                       cases[i].source ? cases[i].source : "axosyslog",
                       cases[i].sourcetype ? cases[i].sourcetype : "axosyslog",
                       cases[i].host ? cases[i].host : host_name);
      cr_assert_eq(message->timestamp, 1700000000 + i, "timestamp mismatch for case %zu", i);
    }

  g_ptr_array_free(messages, TRUE);
  g_string_free(wire, TRUE);
  g_string_free(big, TRUE);
  log_proto_server_free(server);
}


static GPtrArray *pump_keepalive;

static void
_pump(LogTransport *from, LogTransport *to)
{
  gchar buf[4096];
  gssize rc;

  while ((rc = log_transport_mock_read_from_write_buffer((LogTransportMock *) from, buf, sizeof(buf))) > 0)
    {
      /* inject_data keeps the pointer, so hand it a copy that outlives the read */
      gchar *chunk = g_memdup2(buf, rc);
      g_ptr_array_add(pump_keepalive, chunk);
      log_transport_mock_inject_data((LogTransportMock *) to, chunk, rc);
    }
}

Test(splunk_s2s_roundtrip, live_session_delivers_events_and_flows_acks_back)
{
  pump_keepalive = g_ptr_array_new_with_free_func(g_free);

  LogTransport *client_transport = log_transport_mock_endless_records_new(LTM_EOF);
  LogTransport *server_transport = log_transport_mock_endless_records_new(LTM_EOF);

  LogProtoClient *client = _client_new(client_transport);
  LogProtoServer *server = _server_new(server_transport);

  for (gint i = 0; i < 200 && !_client_handshake_done(client); i++)
    {
      cr_assert_eq(log_proto_client_flush(client), LPS_SUCCESS);
      _pump(client_transport, server_transport);

      LogProtoStatus status;
      cr_assert_null(_fetch_once(server, &status), "no message expected during the handshake");
      cr_assert_neq(status, LPS_ERROR, "server handshake failed");
      _pump(server_transport, client_transport);

      cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
    }
  cr_assert(_client_handshake_done(client), "handshake did not converge");

  const gint n_events = 5;
  for (gint i = 0; i < n_events; i++)
    {
      LogMessage *msg = _msg_new("idx", "src", "st", "h", 1700000000 + i);
      gboolean consumed = FALSE;
      gchar *payload = g_strdup_printf("event %d", i);
      cr_assert_eq(log_proto_client_post(client, msg, (guchar *) payload, strlen(payload), &consumed), LPS_SUCCESS);
      cr_assert(consumed);
      log_msg_unref(msg);
    }
  _pump(client_transport, server_transport);

  guint expected = 1 + n_events;
  GPtrArray *messages = _collect_messages(server, expected);
  cr_assert_eq(messages->len, expected, "expected %u messages, got %u", expected, messages->len);
  for (gint i = 0; i < n_events; i++)
    {
      DecodedMessage *message = g_ptr_array_index(messages, i + 1);
      gchar *want = g_strdup_printf("event %d", i);
      cr_assert_arr_eq(message->raw->str, want, message->raw->len);
      g_free(want);
    }

  /* the server emitted acks while decoding; carry them back and let the
   * client confirm the whole batch */
  _pump(server_transport, client_transport);
  cr_assert_eq(log_proto_client_process_in(client), LPS_SUCCESS);
  cr_assert_eq(acks, n_events, "indexer acks did not confirm every event");
  cr_assert_eq(rewinds, 0);

  g_ptr_array_free(messages, TRUE);
  log_proto_server_free(server);
  log_proto_client_free(client);
  g_ptr_array_free(pump_keepalive, TRUE);
}

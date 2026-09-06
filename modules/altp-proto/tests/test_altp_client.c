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

#include "libtest/config_parse_lib.h"
#include "libtest/grab-logging.h"
#include "libtest/mock-transport.h"
#include "libtest/persist_lib.h"

#include "altp-proto-options.h"
#include "logproto-altp-client.h"

#include "apphook.h"
#include "cfg.h"
#include "driver.h"
#include "plugin.h"
#include "transport/transport-stack.h"

#include <errno.h>
#include <string.h>
#include <zlib.h>

#define ALTP_BANNER              "220 ALTP 1.0\n"
#define ALTP_NO_CAPABILITIES     "250 \n"
#define ALTP_CAPABILITY_LIST     "250 STARTTLS\n"
#define ALTP_CAPABILITY_ZLIB     "250 ZLIB\n"
#define ALTP_CAPABILITY_BOTH     "250-STARTTLS\n250 ZLIB\n"
#define ALTP_READY_TO_START_TLS  "250 Ready to start TLS\n"
#define ALTP_READY_TO_START_ZLIB "250 Ready to start ZLIB\n"
#define ALTP_READY               "250 Ready\n"

#define ALTP_EHLO                "EHLO 1.0\n"
#define ALTP_STARTTLS            "STARTTLS\n"
#define ALTP_ZLIB                "ZLIB\n"
#define ALTP_DATA                "DATA\n"
#define ALTP_TERMINATOR          ".\n"

/* the persistent name a driver hands us, as afsocket formats it */
#define ALTP_TEST_PERSIST_NAME "afsocket_dd_connections(stream,localhost:35514)"

/* the options a driver hands over are a LogProtoClientOptionsStorage union, the only place our extension fits */
static LogProtoClientOptionsStorage options_storage;

static void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
  cr_assert(cfg_load_module(configuration, "altp_proto"), "cannot load the altp_proto module");
}

static void
teardown(void)
{
  if (configuration)
    cfg_free(configuration);
  configuration = NULL;
  app_shutdown();
}

TestSuite(altp_client, .init = setup, .fini = teardown);

/****************************************************************************
 * A fake TLS transport passing plaintext through, so a test can converse after STARTTLS.
 ****************************************************************************/

typedef struct _FakeTlsTransport
{
  LogTransport super;
  LogTransport *plaintext;
} FakeTlsTransport;

static gssize
_fake_tls_read(LogTransport *s, gpointer buf, gsize count, LogTransportAuxData *aux)
{
  FakeTlsTransport *self = (FakeTlsTransport *) s;

  return log_transport_read(self->plaintext, buf, count, aux);
}

static gssize
_fake_tls_write(LogTransport *s, const gpointer buf, gsize count)
{
  FakeTlsTransport *self = (FakeTlsTransport *) s;

  return log_transport_write(self->plaintext, buf, count);
}

static LogTransport *
_fake_tls_transport_construct(const LogTransportFactory *s, LogTransportStack *stack)
{
  FakeTlsTransport *self = g_new0(FakeTlsTransport, 1);

  log_transport_init_instance(&self->super, "fake-tls", stack->fd);
  self->super.read = _fake_tls_read;
  self->super.write = _fake_tls_write;
  self->plaintext = log_transport_stack_get_transport(stack, LOG_TRANSPORT_INITIAL);

  return &self->super;
}

static LogTransportFactory *
_fake_tls_transport_factory_new(void)
{
  LogTransportFactory *self = g_new0(LogTransportFactory, 1);

  log_transport_factory_init_instance(self, LOG_TRANSPORT_TLS);
  self->construct_transport = _fake_tls_transport_construct;
  return self;
}

/****************************************************************************
 * A transport that accepts only @write_budget octets of a write, so a Frame can be left half written.
 ****************************************************************************/

typedef struct _ThrottledTransport
{
  LogTransport super;
  LogTransport *mock;
  /* octets still accepted, -1 for "as many as offered" */
  gssize write_budget;
  gboolean at_eof;
} ThrottledTransport;

static gssize
_throttled_read(LogTransport *s, gpointer buf, gsize count, LogTransportAuxData *aux)
{
  ThrottledTransport *self = (ThrottledTransport *) s;

  if (self->at_eof)
    return 0;

  return log_transport_read(self->mock, buf, count, aux);
}

static gssize
_throttled_write(LogTransport *s, const gpointer buf, gsize count)
{
  ThrottledTransport *self = (ThrottledTransport *) s;

  if (self->write_budget == 0)
    {
      errno = EAGAIN;
      return -1;
    }

  if (self->write_budget > 0 && (gsize) self->write_budget < count)
    count = self->write_budget;

  gssize written = log_transport_write(self->mock, buf, count);

  if (self->write_budget > 0 && written > 0)
    self->write_budget -= written;

  return written;
}

static void
_throttled_free(LogTransport *s)
{
  ThrottledTransport *self = (ThrottledTransport *) s;

  log_transport_free(self->mock);
  log_transport_free_method(s);
}

static LogTransport *
_throttled_transport_new(LogTransport *mock)
{
  ThrottledTransport *self = g_new0(ThrottledTransport, 1);

  log_transport_init_instance(&self->super, "throttled", 0);
  self->super.read = _throttled_read;
  self->super.write = _throttled_write;
  self->super.free_fn = _throttled_free;
  self->mock = mock;
  self->write_budget = -1;

  return &self->super;
}

/****************************************************************************
 * Parsing transport(altp(...)) the way a destination driver does
 ****************************************************************************/

/* Parse through the client-proto plugin context, which is the production path. */
static LogProtoClientFactory *
_parse_altp_transport(const gchar *config_snippet)
{
  gpointer result = NULL;

  memset(&options_storage, 0, sizeof(options_storage));

  cr_assert(parse_config(config_snippet, LL_CONTEXT_CLIENT_PROTO, &options_storage.super, &result),
            "cannot parse transport(%s)", config_snippet);
  cr_assert_not_null(result, "the altp grammar did not return a LogProtoClientFactory");

  return (LogProtoClientFactory *) result;
}

/* Drive the plugin the way afsocket-grammar.ym does: @tail runs up to and including
 * the ')' the plugin grammar must leave behind -- the only way to reach transport(altp). */
static LogProtoClientFactory *
_parse_altp_transport_tail(const gchar *tail)
{
  CfgLexer *old_lexer = configuration->lexer;
  CFG_LTYPE yylloc;
  CFG_STYPE yylval;

  memset(&yylloc, 0, sizeof(yylloc));
  yylloc.first_line = yylloc.last_line = 1;
  yylloc.first_column = yylloc.last_column = 1;

  memset(&options_storage, 0, sizeof(options_storage));

  Plugin *plugin = cfg_find_plugin(configuration, LL_CONTEXT_CLIENT_PROTO, "altp");
  cr_assert_not_null(plugin, "the altp client-proto plugin is not registered");
  cr_assert_not_null(plugin->parser, "the altp plugin must have a grammar of its own");

  CfgLexer *lexer = cfg_lexer_new_buffer(configuration, tail, strlen(tail));
  cr_assert_not_null(lexer);
  configuration->lexer = lexer;
  cfg_lexer_push_context(lexer, main_parser.context, main_parser.keywords, main_parser.name);
  gpointer result = cfg_parse_plugin(configuration, plugin, &yylloc, &options_storage.super);
  cfg_lexer_pop_context(lexer);

  cr_assert_not_null(result, "the altp grammar did not return a LogProtoClientFactory");

  memset(&yylval, 0, sizeof(yylval));
  cr_assert_eq(cfg_lexer_lex(lexer, &yylval, &yylloc), ')',
               "the altp grammar consumed the closing paren of transport()");
  cfg_lexer_free_token(&yylval);

  configuration->lexer = old_lexer;
  cfg_lexer_free(lexer);

  return (LogProtoClientFactory *) result;
}

static AltpProtoClientOptions *
_get_altp_options(void)
{
  return (AltpProtoClientOptions *) &options_storage.super;
}

/****************************************************************************
 * The Receiver's side of a compressed Connection: one zlib stream per direction, sync flush per write (6.2).
 ****************************************************************************/

typedef struct _AltpTestZlib
{
  z_stream deflate_stream;
  z_stream inflate_stream;
  /* the compressed replies stay alive here: the mock borrows the pointers it is injected with */
  GPtrArray *chunks;
} AltpTestZlib;

static void
_free_gstring(gpointer data)
{
  g_string_free((GString *) data, TRUE);
}

static AltpTestZlib *
_zlib_new(void)
{
  AltpTestZlib *self = g_new0(AltpTestZlib, 1);

  cr_assert_eq(deflateInit(&self->deflate_stream, Z_DEFAULT_COMPRESSION), Z_OK);
  cr_assert_eq(inflateInit(&self->inflate_stream), Z_OK);
  self->chunks = g_ptr_array_new_with_free_func(_free_gstring);

  return self;
}

static void
_zlib_free(AltpTestZlib *self)
{
  deflateEnd(&self->deflate_stream);
  inflateEnd(&self->inflate_stream);
  g_ptr_array_free(self->chunks, TRUE);
  g_free(self);
}

static GString *
_zlib_compress(AltpTestZlib *self, const gchar *input)
{
  GString *out = g_string_new("");
  guchar buf[4096];

  self->deflate_stream.next_in = (Bytef *) input;
  self->deflate_stream.avail_in = strlen(input);

  do
    {
      self->deflate_stream.next_out = buf;
      self->deflate_stream.avail_out = sizeof(buf);
      cr_assert_eq(deflate(&self->deflate_stream, Z_SYNC_FLUSH), Z_OK);
      g_string_append_len(out, (const gchar *) buf, sizeof(buf) - self->deflate_stream.avail_out);
    }
  while (self->deflate_stream.avail_out == 0);

  g_ptr_array_add(self->chunks, out);

  return out;
}

static void
_zlib_inflate_into(AltpTestZlib *self, const gchar *data, gsize len, GString *out)
{
  guchar buf[4096];

  self->inflate_stream.next_in = (Bytef *) data;
  self->inflate_stream.avail_in = len;

  while (self->inflate_stream.avail_in > 0)
    {
      self->inflate_stream.next_out = buf;
      self->inflate_stream.avail_out = sizeof(buf);

      gint rc = inflate(&self->inflate_stream, Z_SYNC_FLUSH);
      cr_assert(rc == Z_OK || rc == Z_BUF_ERROR, "the Sender's output is not a zlib stream: %d", rc);
      g_string_append_len(out, (const gchar *) buf, sizeof(buf) - self->inflate_stream.avail_out);

      if (rc == Z_BUF_ERROR)
        break;
    }
}

/****************************************************************************
 * Driving a Sender the way the LogWriter does
 ****************************************************************************/

typedef struct _AltpTestSender
{
  LogTransportMock *mock;
  ThrottledTransport *throttle;
  LogProtoClient *proto;
  GString *written;

  gint acked;
  gint ack_calls;
  gint rewind_calls;
  /* the frames_sent the persistent state held when the ack callback last ran, -1 when there is none */
  gint frames_sent_at_ack;

  PersistState *persist_state;

  /* set once the Connection is compressed: turns the zlib stream back into command text */
  AltpTestZlib *codec;
} AltpTestSender;

static gint
_peek_persisted_frames_sent(AltpTestSender *self)
{
  guint32 frames_sent = 0;

  if (!self->persist_state)
    return -1;

  if (!log_proto_altp_client_load_persisted_state(self->persist_state, ALTP_TEST_PERSIST_NAME, NULL, &frames_sent))
    return -1;

  return (gint) frames_sent;
}

static void
_test_ack(gint num_msg_acked, gpointer user_data)
{
  AltpTestSender *self = (AltpTestSender *) user_data;

  self->acked += num_msg_acked;
  self->ack_calls++;
  self->frames_sent_at_ack = _peek_persisted_frames_sent(self);
}

static void
_test_rewind(gpointer user_data)
{
  AltpTestSender *self = (AltpTestSender *) user_data;

  self->rewind_calls++;
}

/* Set a Sender up the way afsocket_dd_connected() does. */
static void
_sender_init(AltpTestSender *self, LogProtoClientFactory *factory, gboolean with_tls, PersistState *persist_state)
{
  memset(self, 0, sizeof(*self));

  /* LogTransportMock is opaque in libtest, so the same object is kept in both guises */
  LogTransport *mock_transport = log_transport_mock_endless_records_new(LTM_EOF);

  self->mock = (LogTransportMock *) mock_transport;
  self->throttle = (ThrottledTransport *) _throttled_transport_new(mock_transport);
  self->proto = log_proto_client_factory_construct(factory, &self->throttle->super, &options_storage.super);
  cr_assert_not_null(self->proto);

  if (with_tls)
    log_transport_stack_add_factory(&self->proto->transport_stack, _fake_tls_transport_factory_new());

  LogProtoClientFlowControlFuncs flow_control_funcs;
  flow_control_funcs.ack_callback = _test_ack;
  flow_control_funcs.rewind_callback = _test_rewind;
  flow_control_funcs.user_data = self;
  log_proto_client_set_client_flow_control(self->proto, &flow_control_funcs);

  self->persist_state = persist_state;
  self->frames_sent_at_ack = -1;
  self->written = g_string_new("");

  /* afsocket calls this for every Connection and ignores its return value */
  log_proto_client_restart_with_state(self->proto, persist_state, ALTP_TEST_PERSIST_NAME);
}

static void
_sender_deinit(AltpTestSender *self)
{
  log_proto_client_free(self->proto);
  g_string_free(self->written, TRUE);
  if (self->codec)
    _zlib_free(self->codec);
}

static void
_collect_written(AltpTestSender *self)
{
  gchar buffer[4096];
  gssize len;

  while ((len = log_transport_mock_read_from_write_buffer(self->mock, buffer, sizeof(buffer))) > 0)
    {
      if (self->codec)
        _zlib_inflate_into(self->codec, buffer, len, self->written);
      else
        g_string_append_len(self->written, buffer, len);
    }
}

static LogProtoStatus
_reply(AltpTestSender *self, const gchar *reply)
{
  log_transport_mock_inject_data(self->mock, reply, -1);

  LogProtoStatus status = log_proto_client_process_in(self->proto);

  _collect_written(self);

  return status;
}

static LogProtoStatus
_reply_compressed(AltpTestSender *self, const gchar *reply)
{
  GString *compressed = _zlib_compress(self->codec, reply);

  log_transport_mock_inject_data(self->mock, compressed->str, compressed->len);

  LogProtoStatus status = log_proto_client_process_in(self->proto);

  _collect_written(self);

  return status;
}

static LogProtoStatus
_process_in(AltpTestSender *self)
{
  LogProtoStatus status = log_proto_client_process_in(self->proto);

  _collect_written(self);

  return status;
}

/* Hand a message over the way log_writer_write_message() does: the proto takes ownership of the payload. */
static LogProtoStatus
_post(AltpTestSender *self, const gchar *payload, gboolean *consumed)
{
  guchar *msg = (guchar *) g_strdup(payload);
  LogProtoStatus status = log_proto_client_post(self->proto, NULL, msg, strlen(payload), consumed);

  if (!*consumed)
    g_free(msg);

  _collect_written(self);

  return status;
}

static void
_post_and_assert_consumed(AltpTestSender *self, const gchar *payload)
{
  gboolean consumed = FALSE;

  cr_assert_eq(_post(self, payload, &consumed), LPS_SUCCESS);
  cr_assert(consumed, "the Sender refused a message of an open Batch");
}

static LogProtoStatus
_flush(AltpTestSender *self)
{
  LogProtoStatus status = log_proto_client_flush(self->proto);

  _collect_written(self);

  return status;
}

static void
_assert_written(AltpTestSender *self, const gchar *expected)
{
  cr_assert_eq(self->written->len, strlen(expected),
               "the Sender wrote %u octets, not %u: <%s>",
               (guint) self->written->len, (guint) strlen(expected), self->written->str);
  cr_assert_arr_eq(self->written->str, expected, strlen(expected));
  g_string_truncate(self->written, 0);
}

static gchar *
_extract_session_id(AltpTestSender *self)
{
  const gchar *sync = strstr(self->written->str, "SYNC ");

  cr_assert_not_null(sync, "the Sender did not send SYNC: <%s>", self->written->str);
  sync += strlen("SYNC ");

  const gchar *lf = strchr(sync, '\n');
  cr_assert_not_null(lf);

  return g_strndup(sync, lf - sync);
}

/* Banner, EHLO, SYNC and DATA on a fresh Session with no Capability; returns the Session ID. */
static gchar *
_negotiate(AltpTestSender *self)
{
  cr_assert_eq(_reply(self, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(self, ALTP_EHLO);

  cr_assert_eq(_reply(self, ALTP_NO_CAPABILITIES), LPS_SUCCESS);
  gchar *session_id = _extract_session_id(self);
  g_string_truncate(self->written, 0);

  cr_assert_eq(_reply(self, "250 Received 0\n"), LPS_SUCCESS);
  _assert_written(self, ALTP_DATA);

  cr_assert_eq(_reply(self, ALTP_READY), LPS_SUCCESS);
  _assert_written(self, "");

  /* the SYNC acknowledgement already drained the (empty) backlog: count only what follows */
  self->acked = 0;
  self->ack_calls = 0;
  self->rewind_calls = 0;

  return session_id;
}

/****************************************************************************
 * The plugin, the factory and the option block
 ****************************************************************************/

Test(altp_client, the_altp_plugin_is_registered_in_the_client_proto_context)
{
  Plugin *plugin = cfg_find_plugin(configuration, LL_CONTEXT_CLIENT_PROTO, "altp");

  cr_assert_not_null(plugin);
  cr_assert_not_null(plugin->parser, "the altp client transport must have a grammar of its own");
}

Test(altp_client, the_factory_carries_the_altp_defaults_for_the_driver)
{
  LogProtoClientFactory *factory = _parse_altp_transport("altp()");

  cr_assert_eq(factory->default_inet_port, ALTP_DEFAULT_PORT);
  cr_assert(log_proto_client_factory_is_proto_stateful(factory),
            "the Sender retains its Frames across Connections, so its factory must be stateful");
}

Test(altp_client, an_empty_option_block_yields_the_specification_defaults)
{
  _parse_altp_transport("altp()");

  cr_assert_eq(_get_altp_options()->altp.ack_timeout, ALTP_DEFAULT_ACK_TIMEOUT);
  cr_assert_eq(_get_altp_options()->altp.max_frame_size, ALTP_SENDER_DEFAULT_MAX_FRAME_SIZE);
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_AUTO);
  cr_assert_eq(_get_altp_options()->altp.response_timeout, ALTP_DEFAULT_ACK_TIMEOUT);
  cr_assert_eq(_get_altp_options()->altp.batch_size, 0,
               "without batch-size() the bound comes from flush-lines() of the driver");
}

Test(altp_client, the_full_option_block_is_parsed)
{
  _parse_altp_transport("altp(ack-timeout(120) max-frame-size(8192) tls-policy(required))");

  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 120);
  cr_assert_eq(_get_altp_options()->altp.max_frame_size, 8192);
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_REQUIRED);
}

Test(altp_client, the_tls_policy_values_are_parsed)
{
  _parse_altp_transport("altp(tls-policy(optional))");
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_OPTIONAL);

  _parse_altp_transport("altp(tls-policy(none))");
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_NONE);
}

Test(altp_client, transport_altp_without_an_option_block_means_all_defaults)
{
  LogProtoClientFactory *factory = _parse_altp_transport_tail(")");

  cr_assert_not_null(factory);
  cr_assert_eq(_get_altp_options()->altp.ack_timeout, ALTP_DEFAULT_ACK_TIMEOUT);
  cr_assert_eq(_get_altp_options()->altp.max_frame_size, ALTP_SENDER_DEFAULT_MAX_FRAME_SIZE);
}

Test(altp_client, transport_altp_with_an_option_block_leaves_the_transport_paren_behind)
{
  _parse_altp_transport_tail("(ack-timeout(30)))");

  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 30);
}

static void
_assert_destination_parses(const gchar *config_snippet)
{
  LogDriver *driver = NULL;

  cr_assert(cfg_load_module(configuration, "afsocket"), "cannot load the afsocket module");
  cr_assert(parse_config(config_snippet, LL_CONTEXT_DESTINATION, NULL, (gpointer *) &driver),
            "cannot parse destination %s", config_snippet);
  cr_assert_not_null(driver);

  log_pipe_unref(&driver->super);
}

Test(altp_client, a_network_destination_accepts_transport_altp_without_an_option_block)
{
  _assert_destination_parses("network(\"localhost\" transport(altp))");
}

Test(altp_client, a_network_destination_accepts_the_full_altp_option_block)
{
  _assert_destination_parses("network(\"localhost\" port(35514) "
                             "transport(altp(ack-timeout(900) max-frame-size(65536))) "
                             "flush-lines(1000))");
}

Test(altp_client, a_syslog_destination_accepts_transport_altp)
{
  _assert_destination_parses("syslog(\"localhost\" transport(altp(ack-timeout(60))))");
}

/****************************************************************************
 * The spellings of existing ALTP deployments
 ****************************************************************************/

Test(altp_client, tls_required_yes_means_the_required_policy)
{
  _parse_altp_transport("altp(tls_required(yes))");

  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_REQUIRED);
}

Test(altp_client, tls_required_no_means_the_none_policy)
{
  _parse_altp_transport("altp(tls_required(no))");

  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_NONE);
}

Test(altp_client, allow_plain_compress_and_compress_level_are_accepted)
{
  _parse_altp_transport("altp(allow_plain_compress(yes) compress_level(6))");

  cr_assert(_get_altp_options()->altp.compression);
  cr_assert_eq(_get_altp_options()->altp.compression_level, 6);
}

/* the lexer is what warns about an obsoleted keyword, once per process */
Test(altp_client, allow_compress_is_accepted_with_a_warning)
{
  start_grabbing_messages();
  _parse_altp_transport("altp(allow_compress(yes))");
  stop_grabbing_messages();

  cr_assert(_get_altp_options()->altp.compression);
  assert_grabbed_log_contains("obsoleted keyword");
}

Test(altp_client, message_acknowledgement_timeout_is_the_acknowledgement_timeout)
{
  _parse_altp_transport("altp(message_acknowledgement_timeout(30))");

  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 30);
}

Test(altp_client, response_timeout_is_parsed)
{
  _parse_altp_transport("altp(response_timeout(30))");

  cr_assert_eq(_get_altp_options()->altp.response_timeout, 30);
  cr_assert_eq(_get_altp_options()->altp.ack_timeout, ALTP_DEFAULT_ACK_TIMEOUT);
}

Test(altp_client, batch_size_is_parsed)
{
  _parse_altp_transport("altp(batch_size(100))");

  cr_assert_eq(_get_altp_options()->altp.batch_size, 100);
}

Test(altp_client, flush_lines_inside_the_option_block_is_the_batch_size)
{
  _parse_altp_transport("altp(flush_lines(100))");

  cr_assert_eq(_get_altp_options()->altp.batch_size, 100);
}

Test(altp_client, flush_timeout_is_accepted_and_has_no_effect)
{
  _parse_altp_transport("altp(flush_timeout(10))");

  cr_assert_eq(_get_altp_options()->altp.batch_size, 0);
  cr_assert_eq(_get_altp_options()->altp.ack_timeout, ALTP_DEFAULT_ACK_TIMEOUT);
}

Test(altp_client, a_network_destination_accepts_the_legacy_option_block)
{
  _assert_destination_parses("network(\"localhost\" port(35514) "
                             "transport(altp(tls_required(yes) allow_plain_compress(yes) compress_level(6) "
                             "batch_size(100) message_acknowledgement_timeout(30) response_timeout(30) "
                             "flush_timeout(10))))");
}

/****************************************************************************
 * Connection establishment (5, 6.1, 7.2)
 ****************************************************************************/

Test(altp_client, nothing_is_written_before_the_banner_arrives)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_process_in(&sender), LPS_SUCCESS);
  _assert_written(&sender, "");

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  _sender_deinit(&sender);
}

Test(altp_client, the_banner_text_is_not_parsed)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  /* a legacy Receiver greets with the retired name of the protocol */
  cr_assert_eq(_reply(&sender, "220 RLTP 1.0\n"), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  _sender_deinit(&sender);
}

Test(altp_client, an_empty_capability_list_is_followed_by_sync_with_the_session_id)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_SUCCESS);

  gchar *session_id = _extract_session_id(&sender);
  gchar *expected = g_strdup_printf("SYNC %s\n", session_id);

  _assert_written(&sender, expected);
  cr_assert_eq(strlen(session_id), 32, "a Session ID is 16 octets of hexadecimal: <%s>", session_id);
  for (gsize i = 0; i < strlen(session_id); i++)
    cr_assert(g_ascii_isxdigit(session_id[i]) && !g_ascii_isupper(session_id[i]),
              "a Session ID is lowercase hexadecimal: <%s>", session_id);

  g_free(expected);
  g_free(session_id);
  _sender_deinit(&sender);
}

Test(altp_client, an_unknown_capability_is_ignored)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  cr_assert_eq(_reply(&sender, "250-ZLIB\n250 SOMETHING\n"), LPS_SUCCESS);

  gchar *session_id = _extract_session_id(&sender);
  cr_assert_eq(strlen(session_id), 32);

  g_free(session_id);
  _sender_deinit(&sender);
}

Test(altp_client, starttls_is_requested_and_the_capabilities_are_read_again_after_the_upgrade)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), TRUE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  cr_assert_eq(_reply(&sender, ALTP_CAPABILITY_LIST), LPS_SUCCESS);
  _assert_written(&sender, ALTP_STARTTLS);

  cr_assert_eq(_reply(&sender, ALTP_READY_TO_START_TLS), LPS_SUCCESS);
  cr_assert_eq(sender.proto->transport_stack.active_transport, LOG_TRANSPORT_TLS,
               "the Sender did not switch the Connection to TLS");
  _assert_written(&sender, ALTP_EHLO);

  /* STARTTLS is not offered again on the upgraded Connection, so the Session is opened */
  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_SUCCESS);

  gchar *session_id = _extract_session_id(&sender);
  cr_assert_eq(strlen(session_id), 32);

  g_free(session_id);
  _sender_deinit(&sender);
}

Test(altp_client, a_receiver_that_does_not_offer_starttls_is_refused_under_tls_policy_required)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(tls-policy(required))"), TRUE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  /* the Sender aborts rather than continuing in plaintext (6.1) */
  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_ERROR);
  _assert_written(&sender, "");

  _sender_deinit(&sender);
}

/* tls() alone is the opportunistic policy: STARTTLS when it is on offer, and
 * the Session in plaintext when it is not (ADR-0011) */
Test(altp_client, a_receiver_that_does_not_offer_starttls_is_talked_to_in_the_clear)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), TRUE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  start_grabbing_messages();
  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_SUCCESS);
  stop_grabbing_messages();

  assert_grabbed_log_contains("does not offer the STARTTLS Capability, continuing in plaintext");
  cr_assert_eq(sender.proto->transport_stack.active_transport, LOG_TRANSPORT_INITIAL);

  gchar *session_id = _extract_session_id(&sender);
  g_free(session_id);
  _sender_deinit(&sender);
}

Test(altp_client, tls_policy_none_ignores_an_advertised_starttls)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(tls-policy(none))"), TRUE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  cr_assert_eq(_reply(&sender, ALTP_CAPABILITY_LIST), LPS_SUCCESS);
  cr_assert_null(strstr(sender.written->str, ALTP_STARTTLS),
                 "the Sender requested STARTTLS under tls-policy(none): <%s>", sender.written->str);

  gchar *session_id = _extract_session_id(&sender);
  g_free(session_id);
  _sender_deinit(&sender);
}

Test(altp_client, tls_policy_required_without_a_tls_block_is_a_configuration_error)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(tls-policy(required))"), FALSE, NULL);

  start_grabbing_messages();
  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_ERROR);
  stop_grabbing_messages();

  assert_grabbed_log_contains("needs a tls() block");
  _assert_written(&sender, "");

  _sender_deinit(&sender);
}

Test(altp_client, tls_policy_optional_without_a_tls_block_is_a_configuration_error)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(tls-policy(optional))"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_ERROR);
  _assert_written(&sender, "");

  _sender_deinit(&sender);
}

Test(altp_client, starttls_is_never_requested_without_tls)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  /* STARTTLS is offered but not configured, so it is not invoked (5.3) */
  cr_assert_eq(_reply(&sender, ALTP_CAPABILITY_LIST), LPS_SUCCESS);
  cr_assert_null(strstr(sender.written->str, ALTP_STARTTLS),
                 "the Sender requested STARTTLS without tls(): <%s>", sender.written->str);

  gchar *session_id = _extract_session_id(&sender);
  g_free(session_id);
  _sender_deinit(&sender);
}

/****************************************************************************
 * ZLIB (6.2)
 ****************************************************************************/

Test(altp_client, compression_is_off_by_default_and_level_6)
{
  _parse_altp_transport("altp()");

  cr_assert_not(_get_altp_options()->altp.compression);
  cr_assert_eq(_get_altp_options()->altp.compression_level, ALTP_DEFAULT_COMPRESSION_LEVEL);
}

Test(altp_client, compression_and_compression_level_are_parsed)
{
  _parse_altp_transport("altp(compression(yes) compression-level(1))");

  cr_assert(_get_altp_options()->altp.compression);
  cr_assert_eq(_get_altp_options()->altp.compression_level, 1);
}

Test(altp_client, zlib_is_never_requested_without_compression)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  cr_assert_eq(_reply(&sender, ALTP_CAPABILITY_ZLIB), LPS_SUCCESS);
  cr_assert_null(strstr(sender.written->str, ALTP_ZLIB),
                 "the Sender requested ZLIB without compression(): <%s>", sender.written->str);

  gchar *session_id = _extract_session_id(&sender);
  g_free(session_id);
  _sender_deinit(&sender);
}

/* a Capability the most recent EHLO reply did not advertise MUST NOT be invoked (5.3, 6.2) */
Test(altp_client, a_receiver_that_does_not_offer_zlib_is_talked_to_in_the_clear)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(compression(yes))"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_SUCCESS);
  cr_assert_null(strstr(sender.written->str, ALTP_ZLIB),
                 "the Sender requested an unadvertised ZLIB: <%s>", sender.written->str);
  cr_assert_eq(sender.proto->transport_stack.active_transport, LOG_TRANSPORT_INITIAL);

  gchar *session_id = _extract_session_id(&sender);
  g_free(session_id);
  _sender_deinit(&sender);
}

/* the codec takes over at the LF of `250 Ready to start ZLIB`, so the SYNC is already deflated */
static gchar *
_negotiate_zlib(AltpTestSender *sender)
{
  cr_assert_eq(_reply(sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(sender, ALTP_EHLO);

  cr_assert_eq(_reply(sender, ALTP_CAPABILITY_ZLIB), LPS_SUCCESS);
  _assert_written(sender, ALTP_ZLIB);

  sender->codec = _zlib_new();

  cr_assert_eq(_reply(sender, ALTP_READY_TO_START_ZLIB), LPS_SUCCESS);
  cr_assert_eq(sender->proto->transport_stack.active_transport, LOG_TRANSPORT_ZLIB,
               "the Sender did not switch the Connection to ZLIB");

  gchar *session_id = _extract_session_id(sender);
  g_string_truncate(sender->written, 0);

  return session_id;
}

Test(altp_client, a_whole_session_runs_over_the_compressed_stream)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(compression(yes))"), FALSE, NULL);

  gchar *session_id = _negotiate_zlib(&sender);
  cr_assert_eq(strlen(session_id), 32);

  cr_assert_eq(_reply_compressed(&sender, "250 Received 0\n"), LPS_SUCCESS);
  _assert_written(&sender, ALTP_DATA);

  cr_assert_eq(_reply_compressed(&sender, ALTP_READY), LPS_SUCCESS);
  _assert_written(&sender, "");

  _post_and_assert_consumed(&sender, "hello");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  _assert_written(&sender, "5 hello" ALTP_TERMINATOR);

  cr_assert_eq(_reply_compressed(&sender, "250 Received 1\n"), LPS_SUCCESS);
  cr_assert_eq(sender.acked, 1);

  g_free(session_id);
  _sender_deinit(&sender);
}

Test(altp_client, the_frames_written_after_the_switch_are_deflated)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(compression(yes) compression-level(9))"), FALSE, NULL);

  gchar *session_id = _negotiate_zlib(&sender);

  cr_assert_eq(_reply_compressed(&sender, "250 Received 0\n"), LPS_SUCCESS);
  cr_assert_eq(_reply_compressed(&sender, ALTP_READY), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* drop the codec so that `written` keeps the raw zlib octets */
  AltpTestZlib *codec = sender.codec;
  sender.codec = NULL;

  _post_and_assert_consumed(&sender, "hello");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);

  cr_assert_null(strstr(sender.written->str, "5 hello"),
                 "the Frame went on the wire uncompressed");

  GString *inflated = g_string_new("");
  _zlib_inflate_into(codec, sender.written->str, sender.written->len, inflated);
  cr_assert_str_eq(inflated->str, "5 hello" ALTP_TERMINATOR);

  g_string_truncate(sender.written, 0);
  sender.codec = codec;
  g_string_free(inflated, TRUE);
  g_free(session_id);
  _sender_deinit(&sender);
}

Test(altp_client, zlib_is_requested_inside_tls_after_starttls)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(compression(yes))"), TRUE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  /* TLS is settled first, so the ZLIB of this reply is not invoked yet */
  cr_assert_eq(_reply(&sender, ALTP_CAPABILITY_BOTH), LPS_SUCCESS);
  _assert_written(&sender, ALTP_STARTTLS);

  cr_assert_eq(_reply(&sender, ALTP_READY_TO_START_TLS), LPS_SUCCESS);
  cr_assert_eq(sender.proto->transport_stack.active_transport, LOG_TRANSPORT_TLS);
  _assert_written(&sender, ALTP_EHLO);

  cr_assert_eq(_reply(&sender, ALTP_CAPABILITY_ZLIB), LPS_SUCCESS);
  _assert_written(&sender, ALTP_ZLIB);

  sender.codec = _zlib_new();

  cr_assert_eq(_reply(&sender, ALTP_READY_TO_START_ZLIB), LPS_SUCCESS);
  cr_assert_eq(sender.proto->transport_stack.active_transport, LOG_TRANSPORT_ZLIB);

  gchar *session_id = _extract_session_id(&sender);
  g_string_truncate(sender.written, 0);

  cr_assert_eq(_reply_compressed(&sender, "250 Received 0\n"), LPS_SUCCESS);
  _assert_written(&sender, ALTP_DATA);

  g_free(session_id);
  _sender_deinit(&sender);
}

Test(altp_client, data_follows_the_acknowledgement_of_sync_immediately)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);
  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* a fresh Session, and the next Batch is opened at once (ADR-0010) */
  cr_assert_eq(_reply(&sender, "250 Received 0\n"), LPS_SUCCESS);
  _assert_written(&sender, ALTP_DATA);
  cr_assert_eq(sender.acked, 0);

  _sender_deinit(&sender);
}

/****************************************************************************
 * Frames, Batches and their bounds (8)
 ****************************************************************************/

Test(altp_client, frames_are_written_as_a_length_a_space_and_the_payload)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "first message");
  _post_and_assert_consumed(&sender, "second");

  /* no delimiter after a payload: the next header follows it immediately */
  _assert_written(&sender, "13 first message6 second");

  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  _assert_written(&sender, ALTP_TERMINATOR);

  _sender_deinit(&sender);
}

Test(altp_client, a_payload_may_contain_a_lone_dot_and_binary_octets)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  /* Frames are consumed by length, so there is no dot-stuffing (8.2) */
  _post_and_assert_consumed(&sender, ".");
  _post_and_assert_consumed(&sender, ".\n");

  _assert_written(&sender, "1 .2 .\n");

  _sender_deinit(&sender);
}

Test(altp_client, flush_with_no_frame_written_leaves_the_batch_open)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  /* an idle Sender waits inside the open Batch: the Receiver applies no idle timeout there (ADR-0010) */
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  _assert_written(&sender, "");

  _post_and_assert_consumed(&sender, "later");
  _assert_written(&sender, "5 later");

  _sender_deinit(&sender);
}

Test(altp_client, the_frame_count_bound_closes_the_batch)
{
  AltpTestSender sender;

  LogProtoClientFactory *factory = _parse_altp_transport("altp()");

  /* the bound is MIN(flush-lines(), 1000) (8.4) */
  options_storage.super.flush_lines = 2;
  _sender_init(&sender, factory, FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  _post_and_assert_consumed(&sender, "two");
  _assert_written(&sender, "3 one3 two" ALTP_TERMINATOR);

  /* the Batch is closed, so nothing else may be written until it is acknowledged (12.2) */
  gboolean consumed = TRUE;
  cr_assert_eq(_post(&sender, "three", &consumed), LPS_SUCCESS);
  cr_assert_not(consumed, "the Sender wrote a Frame into a closed Batch");
  _assert_written(&sender, "");

  _sender_deinit(&sender);
}

Test(altp_client, batch_size_bounds_the_batch_and_wins_over_flush_lines)
{
  AltpTestSender sender;

  LogProtoClientFactory *factory = _parse_altp_transport("altp(batch-size(2))");

  options_storage.super.flush_lines = 10;
  _sender_init(&sender, factory, FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  _post_and_assert_consumed(&sender, "two");
  _assert_written(&sender, "3 one3 two" ALTP_TERMINATOR);

  gboolean consumed = TRUE;
  cr_assert_eq(_post(&sender, "three", &consumed), LPS_SUCCESS);
  cr_assert_not(consumed, "the Sender wrote a Frame into a closed Batch");

  _sender_deinit(&sender);
}

/* the handshake is what response-timeout() bounds; a Batch keeps waiting for
 * ack-timeout() (9.5) */
Test(altp_client, the_handshake_states_wait_for_the_response_timeout)
{
  AltpTestSender sender;
  GIOCondition cond = 0, idle_cond = 0;
  gint timeout = -1;

  _sender_init(&sender, _parse_altp_transport("altp(ack-timeout(120) response-timeout(30))"), FALSE, NULL);

  cr_assert(log_proto_client_poll_prepare(sender.proto, &cond, &idle_cond, &timeout));
  cr_assert_eq(timeout, 30, "the banner is a reply like any other of the handshake");

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  timeout = -1;
  cr_assert(log_proto_client_poll_prepare(sender.proto, &cond, &idle_cond, &timeout));
  cr_assert_eq(timeout, 30);

  /* the SYNC that follows is acknowledged, not answered: ack-timeout() again */
  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_SUCCESS);

  timeout = -1;
  cr_assert(log_proto_client_poll_prepare(sender.proto, &cond, &idle_cond, &timeout));
  cr_assert_eq(timeout, 120);

  _sender_deinit(&sender);
}

Test(altp_client, a_closed_batch_waits_for_the_acknowledgement_timeout)
{
  AltpTestSender sender;
  GIOCondition cond = 0, idle_cond = 0;
  gint timeout = -1;

  _sender_init(&sender, _parse_altp_transport("altp(ack-timeout(120) response-timeout(30) batch-size(1))"),
               FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  _assert_written(&sender, "3 one" ALTP_TERMINATOR);

  cr_assert(log_proto_client_poll_prepare(sender.proto, &cond, &idle_cond, &timeout));
  cr_assert_eq(timeout, 120);

  _sender_deinit(&sender);
}

Test(altp_client, a_message_is_refused_while_no_batch_is_open)
{
  AltpTestSender sender;
  gboolean consumed = TRUE;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  /* nothing may be written before the banner arrives (5.1) */
  cr_assert_eq(_post(&sender, "message", &consumed), LPS_SUCCESS);
  cr_assert_not(consumed);
  _assert_written(&sender, "");

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  /* nor while a reply is due */
  consumed = TRUE;
  cr_assert_eq(_post(&sender, "message", &consumed), LPS_SUCCESS);
  cr_assert_not(consumed);
  _assert_written(&sender, "");

  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* nor before `250 Ready` answered the DATA (8.1) */
  cr_assert_eq(_reply(&sender, "250 Received 0\n"), LPS_SUCCESS);
  _assert_written(&sender, ALTP_DATA);
  consumed = TRUE;
  cr_assert_eq(_post(&sender, "message", &consumed), LPS_SUCCESS);
  cr_assert_not(consumed);
  _assert_written(&sender, "");

  cr_assert_eq(_reply(&sender, ALTP_READY), LPS_SUCCESS);
  _post_and_assert_consumed(&sender, "message");
  _assert_written(&sender, "7 message");

  _sender_deinit(&sender);
}

/****************************************************************************
 * Acknowledgement (9.4)
 ****************************************************************************/

Test(altp_client, a_complete_acknowledgement_releases_every_frame_of_the_batch)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  _post_and_assert_consumed(&sender, "two");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  _assert_written(&sender, "3 one3 two" ALTP_TERMINATOR);

  cr_assert_eq(_reply(&sender, "250 Received 2\n"), LPS_SUCCESS);
  cr_assert_eq(sender.acked, 2, "the Sender did not release the acknowledged Frames");
  _assert_written(&sender, ALTP_DATA);

  _sender_deinit(&sender);
}

Test(altp_client, a_partial_acknowledgement_releases_the_prefix_and_rewinds_the_rest)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  _post_and_assert_consumed(&sender, "two");
  _post_and_assert_consumed(&sender, "three");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* every Frame beyond the count is undelivered, so the rewind is unconditional (9.3, 9.4) */
  cr_assert_eq(_reply(&sender, "250 Received 1\n"), LPS_SUCCESS);
  cr_assert_eq(sender.acked, 1);
  cr_assert_eq(sender.rewind_calls, 1);
  _assert_written(&sender, ALTP_DATA);

  _sender_deinit(&sender);
}

Test(altp_client, an_acknowledgement_larger_than_the_open_batch_is_clamped)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* the excess is ignored: it can only mean we lost part of our own record (9.4) */
  cr_assert_eq(_reply(&sender, "250 Received 7\n"), LPS_SUCCESS);
  cr_assert_eq(sender.acked, 1, "the Sender released more messages than it had sent Frames");

  _sender_deinit(&sender);
}

Test(altp_client, a_stale_count_reported_by_sync_releases_nothing)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);
  cr_assert_eq(_reply(&sender, ALTP_NO_CAPABILITIES), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* The Receiver resets its counters only on the next command line, so a Batch we
   * already acknowledged is reported again by the next SYNC; frames_sent is 0 here
   * and the count is clamped to it, so the stale count is harmless (9.2, ADR-0004). */
  cr_assert_eq(_reply(&sender, "250 Received 3\n"), LPS_SUCCESS);
  cr_assert_eq(sender.acked, 0);
  _assert_written(&sender, ALTP_DATA);

  _sender_deinit(&sender);
}

Test(altp_client, a_malformed_acknowledgement_closes_the_connection)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* a count may not be guessed (9.4) */
  cr_assert_eq(_reply(&sender, "250 Received lots\n"), LPS_ERROR);
  cr_assert_eq(sender.acked, 0);

  _sender_deinit(&sender);
}

Test(altp_client, a_reply_that_is_not_the_one_due_closes_the_connection)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* `250 Ready` where `250 Received n` was due (14.1) */
  cr_assert_eq(_reply(&sender, ALTP_READY), LPS_ERROR);
  cr_assert_eq(sender.acked, 0);

  _sender_deinit(&sender);
}

/****************************************************************************
 * Reply classes and malformed replies (4.3, 14.1)
 ****************************************************************************/

Test(altp_client, a_4xx_reply_closes_the_connection_cleanly)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* the Receiver abandoned the Batch: nothing acknowledged, nothing disowned, nothing resent (9.3, ADR-0009) */
  cr_assert_eq(_reply(&sender, "421 Try again later\n"), LPS_EOF);
  cr_assert_eq(sender.acked, 0);
  cr_assert_eq(sender.rewind_calls, 0);

  _sender_deinit(&sender);
}

Test(altp_client, a_5xx_reply_closes_the_connection_with_an_error)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  cr_assert_eq(_reply(&sender, "510 Invalid version of dialect\n"), LPS_ERROR);

  _sender_deinit(&sender);
}

Test(altp_client, a_frame_error_retains_every_frame_of_the_batch)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  g_string_truncate(sender.written, 0);

  /* the Frames are reconciled by the next SYNC rather than released or rewound (8.5) */
  cr_assert_eq(_reply(&sender, "552 Frame too large\n"), LPS_ERROR);
  cr_assert_eq(sender.acked, 0);
  cr_assert_eq(sender.rewind_calls, 0);

  _sender_deinit(&sender);
}

Test(altp_client, an_idle_close_of_the_receiver_is_not_an_error)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  g_string_truncate(sender.written, 0);

  /* an idle close is normal termination; everything unacknowledged stays retained (4.4, 14.2) */
  sender.throttle->at_eof = TRUE;
  cr_assert_eq(_process_in(&sender), LPS_EOF);
  cr_assert_eq(sender.acked, 0);
  cr_assert_eq(sender.rewind_calls, 0);

  _sender_deinit(&sender);
}

Test(altp_client, a_malformed_reply_closes_the_connection)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, "2xx not a reply\n"), LPS_ERROR);

  _sender_deinit(&sender);
}

Test(altp_client, a_reply_of_an_unknown_class_closes_the_connection)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  /* a class that is none of 2xx, 4xx or 5xx is malformed (4.3) */
  cr_assert_eq(_reply(&sender, "320 Something else\n"), LPS_ERROR);

  _sender_deinit(&sender);
}

Test(altp_client, a_reply_line_longer_than_the_limit_closes_the_connection)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  gchar *long_reply = g_strdup_printf("250 %0*d", ALTP_MAX_REPLY_LINE, 0);

  cr_assert_eq(_reply(&sender, long_reply), LPS_ERROR);

  g_free(long_reply);
  _sender_deinit(&sender);
}

Test(altp_client, a_reply_terminated_with_crlf_is_tolerated)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  cr_assert_eq(_reply(&sender, "220 ALTP 1.0\r\n"), LPS_SUCCESS);
  _assert_written(&sender, ALTP_EHLO);

  _sender_deinit(&sender);
}

/****************************************************************************
 * Oversized messages (8.5)
 ****************************************************************************/

Test(altp_client, an_oversized_message_is_dropped_rather_than_sent)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(max-frame-size(8))"), FALSE, NULL);
  g_free(_negotiate(&sender));

  gboolean consumed = FALSE;
  cr_assert_eq(_post(&sender, "far too long a payload", &consumed), LPS_SUCCESS);
  cr_assert(consumed, "an oversized message must not be handed back to the writer forever");
  _assert_written(&sender, "");
  /* released without ever being sent: the backlog is acknowledged positionally, so
   * only its oldest entry may be dropped -- and it was the only one */
  cr_assert_eq(sender.acked, 1);

  _post_and_assert_consumed(&sender, "fits");
  _assert_written(&sender, "4 fits");

  _sender_deinit(&sender);
}

Test(altp_client, an_oversized_message_in_the_middle_of_a_batch_closes_it_first)
{
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp(max-frame-size(8))"), FALSE, NULL);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "first");
  _assert_written(&sender, "5 first");

  /* not the oldest entry of the backlog, so it is handed back and the Batch closed */
  gboolean consumed = TRUE;
  cr_assert_eq(_post(&sender, "far too long a payload", &consumed), LPS_SUCCESS);
  cr_assert_not(consumed, "the Sender dropped a message in the middle of a Batch");
  cr_assert_eq(sender.acked, 0, "the Sender released a message that is not the oldest of the backlog");

  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  _assert_written(&sender, ALTP_TERMINATOR);

  /* with the backlog empty the resubmitted message is the oldest again, and may be dropped */
  cr_assert_eq(_reply(&sender, "250 Received 1\n"), LPS_SUCCESS);
  cr_assert_eq(sender.acked, 1);
  _assert_written(&sender, ALTP_DATA);
  cr_assert_eq(_reply(&sender, ALTP_READY), LPS_SUCCESS);

  consumed = FALSE;
  cr_assert_eq(_post(&sender, "far too long a payload", &consumed), LPS_SUCCESS);
  cr_assert(consumed);
  cr_assert_eq(sender.acked, 2, "the oversized message was not released as the first message of the Batch");
  _assert_written(&sender, "");

  _sender_deinit(&sender);
}

/****************************************************************************
 * Partial writes (10.2)
 ****************************************************************************/

Test(altp_client, a_frame_counts_as_sent_only_once_it_is_written_whole)
{
  PersistState *persist_state = clean_and_create_persist_state_for_test("test_altp_client_partial_write.persist");
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, persist_state);
  g_free(_negotiate(&sender));

  /* the Receiver takes the Frame header and two octets of the payload only */
  sender.throttle->write_budget = strlen("7 me");

  gboolean consumed = FALSE;
  cr_assert_eq(_post(&sender, "message", &consumed), LPS_SUCCESS);
  cr_assert(consumed, "the message was taken into the Batch, only its Frame is half written");
  _assert_written(&sender, "7 me");
  cr_assert_eq(_peek_persisted_frames_sent(&sender), 0,
               "a partially written Frame must not be counted");

  GIOCondition cond = 0, idle_cond = 0;
  gint timeout = -1;
  cr_assert(log_proto_client_poll_prepare(sender.proto, &cond, &idle_cond, &timeout));
  cr_assert(cond & G_IO_OUT, "the Sender does not ask to be woken up when it may write again");

  sender.throttle->write_budget = -1;
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  _assert_written(&sender, "ssage" ALTP_TERMINATOR);
  cr_assert_eq(_peek_persisted_frames_sent(&sender), 1);

  _sender_deinit(&sender);
  commit_and_destroy_persist_state(persist_state);
}

/****************************************************************************
 * The persisted Sender state (10.2, 10.3)
 ****************************************************************************/

Test(altp_client, the_session_id_is_persisted_and_reused_by_the_next_connection)
{
  PersistState *persist_state = clean_and_create_persist_state_for_test("test_altp_client_session_id.persist");
  AltpTestSender first;

  _sender_init(&first, _parse_altp_transport("altp()"), FALSE, persist_state);
  gchar *first_session_id = _negotiate(&first);
  _sender_deinit(&first);

  gchar *persisted = NULL;
  cr_assert(log_proto_altp_client_load_persisted_state(persist_state, ALTP_TEST_PERSIST_NAME, &persisted, NULL));
  cr_assert_str_eq(persisted, first_session_id);

  /* the Session ID MUST NOT change across reconnections (7.1, 14.3) */
  AltpTestSender second;

  _sender_init(&second, _parse_altp_transport("altp()"), FALSE, persist_state);
  gchar *second_session_id = _negotiate(&second);
  cr_assert_str_eq(second_session_id, first_session_id);
  _sender_deinit(&second);

  g_free(persisted);
  g_free(second_session_id);
  g_free(first_session_id);
  commit_and_destroy_persist_state(persist_state);
}

Test(altp_client, the_frames_of_an_interrupted_batch_are_reconciled_by_the_next_sync)
{
  PersistState *persist_state = clean_and_create_persist_state_for_test("test_altp_client_resume.persist");
  AltpTestSender first;

  _sender_init(&first, _parse_altp_transport("altp()"), FALSE, persist_state);
  gchar *session_id = _negotiate(&first);

  _post_and_assert_consumed(&first, "one");
  _post_and_assert_consumed(&first, "two");
  _post_and_assert_consumed(&first, "three");
  cr_assert_eq(_peek_persisted_frames_sent(&first), 3);

  /* the Batch is lost unacknowledged: the Frames stay in the backlog, frames_sent stands (9.5, 14.2) */
  _sender_deinit(&first);
  cr_assert_eq(first.acked, 0);
  cr_assert_eq(first.rewind_calls, 0);

  AltpTestSender second;

  _sender_init(&second, _parse_altp_transport("altp()"), FALSE, persist_state);
  cr_assert_eq(_reply(&second, ALTP_BANNER), LPS_SUCCESS);
  _assert_written(&second, ALTP_EHLO);
  cr_assert_eq(_reply(&second, ALTP_NO_CAPABILITIES), LPS_SUCCESS);

  gchar *resumed_session_id = _extract_session_id(&second);
  cr_assert_str_eq(resumed_session_id, session_id);
  g_string_truncate(second.written, 0);

  /* two of the three Frames were made durable; the third is rewound for resend (10.3) */
  cr_assert_eq(_reply(&second, "250 Received 2\n"), LPS_SUCCESS);
  cr_assert_eq(second.acked, 2);
  cr_assert_eq(second.rewind_calls, 1);
  cr_assert_eq(_peek_persisted_frames_sent(&second), 0);
  _assert_written(&second, ALTP_DATA);

  g_free(resumed_session_id);
  g_free(session_id);
  _sender_deinit(&second);
  commit_and_destroy_persist_state(persist_state);
}

Test(altp_client, frames_sent_is_persisted_as_zero_before_the_frames_are_released)
{
  PersistState *persist_state = clean_and_create_persist_state_for_test("test_altp_client_ack_order.persist");
  AltpTestSender sender;

  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, persist_state);
  g_free(_negotiate(&sender));

  _post_and_assert_consumed(&sender, "one");
  _post_and_assert_consumed(&sender, "two");
  cr_assert_eq(_flush(&sender), LPS_SUCCESS);
  cr_assert_eq(_peek_persisted_frames_sent(&sender), 2);
  g_string_truncate(sender.written, 0);

  cr_assert_eq(_reply(&sender, "250 Received 2\n"), LPS_SUCCESS);
  cr_assert_eq(sender.acked, 2);
  /* Step 2 of 9.4 MUST complete before step 3: had we released the Frames
   * first and crashed, the next SYNC would make us release that many further
   * Frames that were never sent. */
  cr_assert_eq(sender.frames_sent_at_ack, 0,
               "frames_sent was not persisted as zero before the acknowledged Frames were released");

  _sender_deinit(&sender);
  commit_and_destroy_persist_state(persist_state);
}

Test(altp_client, a_session_without_a_persistent_state_still_has_a_session_id)
{
  AltpTestSender sender;

  /* what a destination whose persistent state could not be allocated gets */
  _sender_init(&sender, _parse_altp_transport("altp()"), FALSE, NULL);

  gchar *session_id = _negotiate(&sender);
  cr_assert_eq(strlen(session_id), 32);

  g_free(session_id);
  _sender_deinit(&sender);
}

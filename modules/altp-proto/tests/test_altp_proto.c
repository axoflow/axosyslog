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
#include "libtest/fake-time.h"
#include "libtest/grab-logging.h"
#include "libtest/mock-transport.h"
#include "libtest/persist_lib.h"
#include "libtest/proto_lib.h"

#include "altp-proto-options.h"
#include "altp-session.h"
#include "logproto-altp-server.h"

#include "ack-tracker/ack_tracker_factory.h"
#include "apphook.h"
#include "cfg.h"
#include "driver.h"
#include "metrics/metric-names.h"
#include "plugin.h"
#include "stats/stats.h"
#include "stats/stats-cluster-single.h"
#include "stats/stats-registry.h"
#include "transport/transport-stack.h"

#include <string.h>
#include <unistd.h>

#define ALTP_BANNER "220 ALTP 1.0\n"

/* scopes the Session Registry our Connections share (ADR-0005) */
#define ALTP_TEST_PERSIST_NAME "s_altp#0"

/* The altp options only fit into a LogProtoServerOptionsStorage union, unlike
 * libtest's bare proto_server_options, so the tests own their storage. */
static LogProtoServerOptionsStorage options_storage;
static gboolean options_initialized;

static void
setup(void)
{
  app_startup();
  init_proto_tests();
  cr_assert(cfg_load_module(configuration, "altp_proto"), "cannot load the altp_proto module");
}

static void
teardown(void)
{
  if (options_initialized)
    {
      log_proto_server_options_destroy(&options_storage.super);
      options_initialized = FALSE;
    }
  deinit_proto_tests();
  app_shutdown();
}

TestSuite(altp, .init = setup, .fini = teardown);

/****************************************************************************
 * A fake TLS transport, so a test can tell a Connection with tls() from one without.
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
 * Parsing transport(altp(...)) the way a driver does
 ****************************************************************************/

static LogProtoServerFactory *
_parse_altp_transport_into(LogProtoServerOptionsStorage *storage, const gchar *config_snippet)
{
  gpointer result = NULL;

  memset(storage, 0, sizeof(*storage));
  log_proto_server_options_defaults(&storage->super);

  cr_assert(parse_config(config_snippet, LL_CONTEXT_SERVER_PROTO, &storage->super, &result),
            "cannot parse transport(%s)", config_snippet);
  cr_assert_not_null(result, "the altp grammar did not return a LogProtoServerFactory");

  log_proto_server_options_init(&storage->super, configuration);

  return (LogProtoServerFactory *) result;
}

static LogProtoServerFactory *
_parse_altp_transport(const gchar *config_snippet)
{
  LogProtoServerFactory *factory = _parse_altp_transport_into(&options_storage, config_snippet);

  options_initialized = TRUE;
  return factory;
}

/* @tail is the text following the plugin name, up to and including the ')' of
 * transport(), which the plugin grammar must leave behind for its caller */
static LogProtoServerFactory *
_parse_altp_transport_tail(const gchar *tail)
{
  CfgLexer *old_lexer = configuration->lexer;
  CFG_LTYPE yylloc;
  CFG_STYPE yylval;

  memset(&yylloc, 0, sizeof(yylloc));
  yylloc.first_line = yylloc.last_line = 1;
  yylloc.first_column = yylloc.last_column = 1;

  memset(&options_storage, 0, sizeof(options_storage));
  log_proto_server_options_defaults(&options_storage.super);

  Plugin *plugin = cfg_find_plugin(configuration, LL_CONTEXT_SERVER_PROTO, "altp");
  cr_assert_not_null(plugin, "the altp server-proto plugin is not registered");
  cr_assert_not_null(plugin->parser, "the altp plugin must have a grammar of its own");

  CfgLexer *lexer = cfg_lexer_new_buffer(configuration, tail, strlen(tail));
  cr_assert_not_null(lexer);
  configuration->lexer = lexer;
  cfg_lexer_push_context(lexer, main_parser.context, main_parser.keywords, main_parser.name);
  gpointer result = cfg_parse_plugin(configuration, plugin, &yylloc, &options_storage.super);
  cfg_lexer_pop_context(lexer);

  cr_assert_not_null(result, "the altp grammar did not return a LogProtoServerFactory");

  memset(&yylval, 0, sizeof(yylval));
  cr_assert_eq(cfg_lexer_lex(lexer, &yylval, &yylloc), ')',
               "the altp grammar consumed the closing paren of transport()");
  cfg_lexer_free_token(&yylval);

  configuration->lexer = old_lexer;
  cfg_lexer_free(lexer);

  log_proto_server_options_init(&options_storage.super, configuration);
  options_initialized = TRUE;

  return (LogProtoServerFactory *) result;
}

static AltpProtoServerOptions *
_get_altp_options(void)
{
  return (AltpProtoServerOptions *) &options_storage.super;
}

/****************************************************************************
 * Driving a Connection the way the LogReader does
 ****************************************************************************/

typedef enum
{
  /* the proto waits for a durability report or for its acknowledgement timeout */
  ALTP_PUMP_SUSPENDED,
  ALTP_PUMP_REPLIED,
  ALTP_PUMP_FRAMES,
  ALTP_PUMP_EOF,
  ALTP_PUMP_ERROR,
} AltpPumpResult;

typedef struct _AltpTestConnection
{
  LogTransport *transport;
  LogProtoServer *proto;
  GString *replies;
  GPtrArray *frames;
  GPtrArray *bookmarks;
  /* When set, every fetch() is given this one Bookmark, the way the ack tracker
   * hands its pending Bookmark to a fetch() that produces no message. */
  Bookmark *shared_bookmark;
} AltpTestConnection;

static void
_free_bookmark(gpointer data)
{
  Bookmark *bookmark = (Bookmark *) data;

  /* destroying a Bookmark is what releases its Session Record reference */
  bookmark_destroy(bookmark);
  g_free(bookmark);
}

static void
_free_frame(gpointer data)
{
  g_string_free((GString *) data, TRUE);
}

/* Set a Connection up the way afsocket_sc_init() does: construct the proto, set
 * the transport stack up, hand it the persistent state and name of the driver. */
static void
_connection_init_full(AltpTestConnection *self, LogProtoServerFactory *factory,
                      LogProtoServerOptionsStorage *storage, LogTransport *transport, gboolean with_tls,
                      PersistState *persist_state, const gchar *persist_name, StatsClusterKeyBuilder *kb)
{
  memset(self, 0, sizeof(*self));

  self->transport = transport;
  self->proto = log_proto_server_factory_construct(factory, transport, &storage->super, kb);
  cr_assert_not_null(self->proto);

  if (with_tls)
    log_transport_stack_add_factory(&self->proto->transport_stack, _fake_tls_transport_factory_new());

  /* this is what hands the Connection its Session Registry */
  cr_assert(log_proto_server_restart_with_state(self->proto, persist_state, persist_name));

  self->replies = g_string_new("");
  self->frames = g_ptr_array_new_with_free_func(_free_frame);
  self->bookmarks = g_ptr_array_new_with_free_func(_free_bookmark);
}

static void
_connection_init(AltpTestConnection *self, LogProtoServerFactory *factory, LogTransport *transport,
                 gboolean with_tls)
{
  _connection_init_full(self, factory, &options_storage, transport, with_tls, NULL, ALTP_TEST_PERSIST_NAME, NULL);
}

static void
_connection_deinit(AltpTestConnection *self)
{
  log_proto_server_free(self->proto);
  g_ptr_array_free(self->bookmarks, TRUE);
  g_ptr_array_free(self->frames, TRUE);
  g_string_free(self->replies, TRUE);
  if (self->shared_bookmark)
    _free_bookmark(self->shared_bookmark);
}

static void
_collect_written(AltpTestConnection *self)
{
  gchar buffer[4096];
  gssize len;

  while ((len = log_transport_mock_read_from_write_buffer((LogTransportMock *) self->transport, buffer,
                                                          sizeof(buffer))) > 0)
    g_string_append_len(self->replies, buffer, len);
}

/* Run fetch() until the proto suspends itself, ends, errors out, or @wanted_replies
 * octets or @wanted_frames Frames have arrived. */
static AltpPumpResult
_pump_until(AltpTestConnection *self, gsize wanted_replies, guint wanted_frames)
{
  for (gint round = 0; round < 8192; round++)
    {
      GIOCondition cond = 0;
      gint timeout = -1;
      const guchar *msg = NULL;
      gsize msg_len = 0;
      gboolean may_read = TRUE;
      LogTransportAuxData aux;
      LogProtoStatus status;

      if (wanted_replies && self->replies->len >= wanted_replies)
        return ALTP_PUMP_REPLIED;
      if (wanted_frames && self->frames->len >= wanted_frames)
        return ALTP_PUMP_FRAMES;

      if (log_proto_server_poll_prepare(self->proto, &cond, &timeout) == LPPA_SUSPEND)
        return ALTP_PUMP_SUSPENDED;

      Bookmark *bookmark = self->shared_bookmark;
      if (!bookmark)
        bookmark = g_new0(Bookmark, 1);

      log_transport_aux_data_init(&aux);
      status = log_proto_server_fetch(self->proto, &msg, &msg_len, &may_read, &aux, bookmark);
      log_transport_aux_data_destroy(&aux);

      if (msg)
        {
          g_ptr_array_add(self->frames, g_string_new_len((const gchar *) msg, msg_len));
          if (!self->shared_bookmark)
            g_ptr_array_add(self->bookmarks, bookmark);
        }
      else if (!self->shared_bookmark)
        {
          _free_bookmark(bookmark);
        }

      _collect_written(self);

      if (status == LPS_EOF)
        return ALTP_PUMP_EOF;
      if (status == LPS_ERROR)
        return ALTP_PUMP_ERROR;
    }

  cr_assert_fail("the ALTP proto neither suspended itself nor reached a terminal status");
  return ALTP_PUMP_ERROR;
}

static AltpPumpResult
_pump(AltpTestConnection *self)
{
  return _pump_until(self, 0, 0);
}

static void
_assert_replies(AltpTestConnection *self, const gchar *expected)
{
  _pump_until(self, strlen(expected), 0);
  cr_assert_str_eq(self->replies->str, expected);
}

static void
_assert_frame(AltpTestConnection *self, guint index, const gchar *payload, gsize payload_len)
{
  cr_assert_gt(self->frames->len, index, "the ALTP proto returned %u Frames, not %u",
               self->frames->len, index + 1);

  GString *frame = (GString *) g_ptr_array_index(self->frames, index);

  cr_assert_eq(frame->len, payload_len, "Frame %u is %u octets long, not %u",
               index, (guint) frame->len, (guint) payload_len);
  cr_assert_arr_eq(frame->str, payload, payload_len);
}

/* as the consecutive ack tracker does on the destination thread */
static void
_report_durable(AltpTestConnection *self, guint index)
{
  cr_assert_gt(self->bookmarks->len, index);
  bookmark_save((Bookmark *) g_ptr_array_index(self->bookmarks, index));
}

static void
_inject(AltpTestConnection *self, const gchar *input)
{
  log_transport_mock_inject_data((LogTransportMock *) self->transport, input, -1);
}

/* one Connection whose Sender writes everything at once and closes */
static void
_assert_conversation(const gchar *config_snippet, const gchar *input, const gchar *expected_replies,
                     gboolean with_tls)
{
  LogProtoServerFactory *factory = _parse_altp_transport(config_snippet);
  AltpTestConnection conn;

  _connection_init(&conn, factory, log_transport_mock_stream_new(input, -1, LTM_EOF), with_tls);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_EOF, "the Connection did not reach the end of its input");
  cr_assert_str_eq(conn.replies->str, expected_replies);

  _connection_deinit(&conn);
}

/****************************************************************************
 * The plugin, the factory and the options
 ****************************************************************************/

Test(altp, the_altp_plugin_is_registered_in_the_server_proto_context)
{
  Plugin *plugin = cfg_find_plugin(configuration, LL_CONTEXT_SERVER_PROTO, "altp");

  cr_assert_not_null(plugin, "the altp server-proto plugin is not registered");
  cr_assert_not_null(plugin->parser, "the altp plugin must have a grammar of its own");
  cr_assert_null(plugin->construct, "a plugin with a grammar must not have a construct method");
}

Test(altp, the_factory_carries_the_altp_defaults_for_the_driver)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");

  cr_assert_eq(factory->default_inet_port, 35514);
  cr_assert(factory->stateful, "ALTP keeps Session state across Connections");
}

Test(altp, an_empty_option_block_yields_the_specification_defaults)
{
  _parse_altp_transport("altp()");

  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 900);
  cr_assert_eq(_get_altp_options()->altp.session_expiration, 2592000);
  cr_assert_eq(_get_altp_options()->altp.max_sessions, 10000);
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_AUTO);
}

Test(altp, the_full_option_block_is_parsed)
{
  _parse_altp_transport("altp(ack-timeout(120) session-expiration(3600) max-sessions(500) "
                        "tls-policy(required))");

  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 120);
  cr_assert_eq(_get_altp_options()->altp.session_expiration, 3600);
  cr_assert_eq(_get_altp_options()->altp.max_sessions, 500);
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_REQUIRED);
}

Test(altp, max_sessions_zero_is_accepted_and_means_unlimited)
{
  _parse_altp_transport("altp(max-sessions(0))");

  cr_assert_eq(_get_altp_options()->altp.max_sessions, 0);
}

Test(altp, transport_altp_without_an_option_block_means_all_defaults)
{
  LogProtoServerFactory *factory = _parse_altp_transport_tail(")");

  cr_assert_eq(factory->default_inet_port, 35514);
  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 900);
  cr_assert_eq(_get_altp_options()->altp.session_expiration, 2592000);
  cr_assert_eq(_get_altp_options()->altp.max_sessions, 10000);
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_AUTO);
}

Test(altp, transport_altp_with_an_option_block_leaves_the_transport_paren_behind)
{
  _parse_altp_transport_tail("(ack-timeout(30) tls-policy(optional)))");

  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 30);
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_OPTIONAL);
}

Test(altp, tls_policy_optional_is_parsed)
{
  _parse_altp_transport("altp(tls-policy(optional))");

  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_OPTIONAL);
}

/* durability is an absolute prefix count, so ALTP needs the consecutive tracker */
Test(altp, the_options_install_the_consecutive_ack_tracker)
{
  _parse_altp_transport("altp()");

  AckTrackerFactory *factory = options_storage.super.ack_tracker_factory;

  cr_assert_not_null(factory);
  cr_assert_eq(ack_tracker_factory_get_type(factory), ACK_CONSECUTIVE);
}

Test(altp, a_stateful_proto_accepts_the_persistent_state_of_the_driver)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _connection_init(&conn, factory, log_transport_mock_stream_new("", 0, LTM_EOF), FALSE);
  _connection_deinit(&conn);
}

/****************************************************************************
 * The afsocket consumer side: what --syntax-only would check
 ****************************************************************************/

static void
_assert_network_source_parses(const gchar *config_snippet)
{
  LogDriver *driver = NULL;

  cr_assert(cfg_load_module(configuration, "afsocket"), "cannot load the afsocket module");
  cr_assert(parse_config(config_snippet, LL_CONTEXT_SOURCE, NULL, (gpointer *) &driver),
            "cannot parse source %s", config_snippet);
  cr_assert_not_null(driver);

  log_pipe_unref(&driver->super);
}

Test(altp, a_network_source_accepts_transport_altp_without_an_option_block)
{
  _assert_network_source_parses("network(port(35514) transport(altp))");
}

Test(altp, a_network_source_accepts_the_full_altp_option_block)
{
  _assert_network_source_parses("network(port(35514) "
                                "transport(altp(ack-timeout(900) session-expiration(2592000) "
                                "max-sessions(10000) tls-policy(required))) "
                                "log-msg-size(65536) idle-timeout(60))");
}

/****************************************************************************
 * The command phase: banner, EHLO, NOOP and the command errors
 ****************************************************************************/

Test(altp, the_banner_is_written_before_any_command_is_read)
{
  _assert_conversation("altp()", "", ALTP_BANNER, FALSE);
}

Test(altp, noop_is_answered_with_250_ok)
{
  _assert_conversation("altp()", "NOOP\n", ALTP_BANNER "250 OK\n", FALSE);
}

Test(altp, noop_parameters_are_ignored_rather_than_rejected)
{
  _assert_conversation("altp()", "NOOP keep me alive\n", ALTP_BANNER "250 OK\n", FALSE);
}

Test(altp, a_bare_ehlo_means_the_1_0_dialect)
{
  _assert_conversation("altp()", "EHLO\n", ALTP_BANNER "250 \n", FALSE);
}

Test(altp, ehlo_with_a_trailing_space_and_an_empty_version_means_1_0)
{
  _assert_conversation("altp()", "EHLO \n", ALTP_BANNER "250 \n", FALSE);
}

Test(altp, ehlo_1_0_without_tls_advertises_no_capability)
{
  _assert_conversation("altp()", "EHLO 1.0\n", ALTP_BANNER "250 \n", FALSE);
}

Test(altp, ehlo_advertises_starttls_when_the_driver_configured_tls)
{
  _assert_conversation("altp()", "EHLO 1.0\n", ALTP_BANNER "250 STARTTLS\n", TRUE);
}

Test(altp, a_bare_ehlo_advertises_starttls_too)
{
  _assert_conversation("altp()", "EHLO\n", ALTP_BANNER "250 STARTTLS\n", TRUE);
}

Test(altp, ehlo_may_be_repeated_and_each_reply_supersedes_the_previous_one)
{
  _assert_conversation("altp()", "EHLO 1.0\nEHLO\n", ALTP_BANNER "250 \n250 \n", FALSE);
}

Test(altp, an_unsupported_ehlo_version_is_answered_510_and_closes)
{
  _assert_conversation("altp()", "EHLO 2.0\n", ALTP_BANNER "510 Invalid version of dialect\n", FALSE);
}

Test(altp, a_malformed_ehlo_version_is_answered_501_and_closes)
{
  _assert_conversation("altp()", "EHLO 1.x\n", ALTP_BANNER "501 Syntax error\n", FALSE);
}

Test(altp, an_unknown_verb_is_answered_502_and_closes)
{
  _assert_conversation("altp()", "BOGUS\n", ALTP_BANNER "502 Unknown command\n", FALSE);
}

/* ZLIB is not advertised by this Receiver in this milestone, and a Capability
 * that is not offered is answered like any unimplemented verb (6.2). */
Test(altp, zlib_is_answered_502_and_closes)
{
  _assert_conversation("altp()", "ZLIB\n", ALTP_BANNER "502 Unknown command\n", FALSE);
}

Test(altp, a_token_that_merely_begins_with_a_verb_is_not_accepted)
{
  _assert_conversation("altp()", "NOOPS\n", ALTP_BANNER "502 Unknown command\n", FALSE);
}

Test(altp, nothing_is_read_after_a_5xx_reply)
{
  _assert_conversation("altp()", "BOGUS\nNOOP\n", ALTP_BANNER "502 Unknown command\n", FALSE);
}

Test(altp, crlf_line_endings_are_accepted)
{
  _assert_conversation("altp()", "NOOP\r\nEHLO 1.0\r\n", ALTP_BANNER "250 OK\n250 \n", FALSE);
}

Test(altp, an_over_long_command_line_is_answered_501_and_closes)
{
  GString *input = g_string_new("NOOP ");

  /* the limit counts the terminator, so this line plus its LF is one octet too long */
  while (input->len < ALTP_MAX_COMMAND_LINE)
    g_string_append_c(input, 'x');
  g_string_append_c(input, '\n');

  _assert_conversation("altp()", input->str, ALTP_BANNER "501 Syntax error\n", FALSE);
  g_string_free(input, TRUE);
}

Test(altp, a_command_line_of_exactly_the_limit_is_accepted)
{
  GString *input = g_string_new("NOOP ");

  while (input->len < ALTP_MAX_COMMAND_LINE - 1)
    g_string_append_c(input, 'x');
  g_string_append_c(input, '\n');
  cr_assert_eq(input->len, (gsize) ALTP_MAX_COMMAND_LINE);

  _assert_conversation("altp()", input->str, ALTP_BANNER "250 OK\n", FALSE);
  g_string_free(input, TRUE);
}

/****************************************************************************
 * STARTTLS (specification 6.1)
 ****************************************************************************/

Test(altp, starttls_switches_the_transport_stack_once_the_reply_is_flushed)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _connection_init(&conn, factory, log_transport_mock_endless_stream_new("STARTTLS\n", -1, LTM_EOF), TRUE);

  _assert_replies(&conn, ALTP_BANNER "250 Ready to start TLS\n");
  cr_assert_eq(conn.proto->transport_stack.active_transport, LOG_TRANSPORT_TLS,
               "the TLS handshake must start once the reply has been written");

  _connection_deinit(&conn);
}

/* the fake TLS transport reads the same mock stream, so a command injected after
 * the switch is what arrives encrypted */
static void
_start_tls(AltpTestConnection *conn, LogProtoServerFactory *factory)
{
  _connection_init(conn, factory, log_transport_mock_endless_stream_new("STARTTLS\n", -1, LTM_EOF), TRUE);

  _assert_replies(conn, ALTP_BANNER "250 Ready to start TLS\n");
  cr_assert_eq(conn->proto->transport_stack.active_transport, LOG_TRANSPORT_TLS);
}

Test(altp, a_second_starttls_is_answered_506_and_closes)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _start_tls(&conn, factory);

  _inject(&conn, "STARTTLS\n");
  _assert_replies(&conn, ALTP_BANNER "250 Ready to start TLS\n506 Already using TLS\n");

  _connection_deinit(&conn);
}

/* the Capabilities are re-evaluated after the upgrade (5.2, 6.1) */
Test(altp, ehlo_after_starttls_no_longer_advertises_starttls)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _start_tls(&conn, factory);

  _inject(&conn, "EHLO 1.0\n");
  _assert_replies(&conn, ALTP_BANNER "250 Ready to start TLS\n250 \n");

  _connection_deinit(&conn);
}

/* No plaintext octet follows the STARTTLS command line (6.1): whatever a peer
 * pipelined after it -- or an on-path attacker appended -- MUST NOT be dispatched
 * as a command of the TLS Session, the plaintext injection of CVE-2011-0411. */
Test(altp, plaintext_pipelined_after_starttls_is_discarded)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  /* a record mock, so both lines arrive in the read that ends the STARTTLS line */
  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("STARTTLS\nSYNC evil\n", -1, LTM_EOF), TRUE);

  _assert_replies(&conn, ALTP_BANNER "250 Ready to start TLS\n");
  cr_assert_eq(conn.proto->transport_stack.active_transport, LOG_TRANSPORT_TLS);

  /* had the SYNC been dispatched, DATA would have been answered `250 Ready` */
  _inject(&conn, "DATA\n");
  _assert_replies(&conn, ALTP_BANNER "250 Ready to start TLS\n503 Need SYNC before use this command\n");

  cr_assert_eq(altp_session_registry_get_session_count(
                 altp_receiver_context_get_registry(_get_altp_options()->context)), 0,
               "the Session of a discarded plaintext SYNC may not be created");

  _connection_deinit(&conn);
}

Test(altp, starttls_without_a_tls_factory_is_answered_502_and_closes)
{
  _assert_conversation("altp()", "STARTTLS\n", ALTP_BANNER "502 Unknown command\n", FALSE);
}

Test(altp, starttls_with_parameters_is_answered_501_and_closes)
{
  _assert_conversation("altp()", "STARTTLS now\n", ALTP_BANNER "501 Syntax error\n", TRUE);
}

/****************************************************************************
 * SYNC (specification 7.2)
 ****************************************************************************/

Test(altp, sync_of_an_unknown_session_is_answered_with_a_zero_count)
{
  _assert_conversation("altp()", "SYNC 9f2c1ab4e77d05836b1e0f4c2d9a8571\n",
                       ALTP_BANNER "250 Received 0\n", FALSE);
}

Test(altp, sync_may_be_repeated_with_the_same_session_id)
{
  _assert_conversation("altp()", "SYNC s1\nSYNC s1\n",
                       ALTP_BANNER "250 Received 0\n250 Received 0\n", FALSE);
}

Test(altp, sync_with_an_empty_session_id_is_answered_504_and_closes)
{
  _assert_conversation("altp()", "SYNC\n", ALTP_BANNER "504 Invalid session id\n", FALSE);
  _assert_conversation("altp()", "SYNC \n", ALTP_BANNER "504 Invalid session id\n", FALSE);
}

Test(altp, sync_with_an_over_long_session_id_is_answered_504_and_closes)
{
  GString *input = g_string_new("SYNC ");

  for (gint i = 0; i < 65; i++)
    g_string_append_c(input, 'a');
  g_string_append_c(input, '\n');

  _assert_conversation("altp()", input->str, ALTP_BANNER "504 Invalid session id\n", FALSE);
  g_string_free(input, TRUE);
}

Test(altp, a_session_id_of_exactly_64_octets_is_accepted)
{
  GString *input = g_string_new("SYNC ");

  for (gint i = 0; i < 64; i++)
    g_string_append_c(input, 'a');
  g_string_append_c(input, '\n');

  _assert_conversation("altp()", input->str, ALTP_BANNER "250 Received 0\n", FALSE);
  g_string_free(input, TRUE);
}

/* octets outside 0x21..0x7e are rejected, which a Receiver may do (7.2) */
Test(altp, a_session_id_with_an_out_of_range_octet_is_answered_504_and_closes)
{
  _assert_conversation("altp()", "SYNC s1 s2\n", ALTP_BANNER "504 Invalid session id\n", FALSE);
  _assert_conversation("altp()", "SYNC s\x01one\n", ALTP_BANNER "504 Invalid session id\n", FALSE);
}

Test(altp, a_second_sync_with_another_session_id_is_answered_511_and_closes)
{
  _assert_conversation("altp()", "SYNC s1\nSYNC s2\n",
                       ALTP_BANNER "250 Received 0\n"
                       "511 Another session is already bound to this connection\n", FALSE);
}

Test(altp, sync_over_plaintext_is_answered_505_when_the_receiver_requires_tls)
{
  _assert_conversation("altp(tls-policy(required))", "SYNC s1\n",
                       ALTP_BANNER "505 STARTTLS required\n", FALSE);
}

/* tls() selects the required policy unless tls-policy() says otherwise (ADR-0008) */
Test(altp, tls_configured_without_a_policy_requires_starttls_before_sync)
{
  _assert_conversation("altp()", "SYNC s1\n", ALTP_BANNER "505 STARTTLS required\n", TRUE);
}

Test(altp, tls_policy_optional_admits_a_plaintext_session)
{
  _assert_conversation("altp(tls-policy(optional))", "SYNC s1\n",
                       ALTP_BANNER "250 Received 0\n", TRUE);
}

/****************************************************************************
 * DATA, Frames and the Batch terminator (specification 8)
 ****************************************************************************/

Test(altp, data_without_a_successful_sync_is_answered_503_and_closes)
{
  _assert_conversation("altp()", "DATA\n",
                       ALTP_BANNER "503 Need SYNC before use this command\n", FALSE);
}

Test(altp, an_empty_batch_is_acknowledged_with_a_zero_count)
{
  _assert_conversation("altp()", "SYNC s1\nDATA\n.\n",
                       ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 0\n", FALSE);
}

Test(altp, a_batch_is_acknowledged_only_once_every_frame_is_durable)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1,
                                                          "3 abc3 def3 ghi.\n", -1, LTM_EOF),
                   FALSE);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED, "the Receiver must wait for durability");
  cr_assert_str_eq(conn.replies->str, ALTP_BANNER "250 Received 0\n250 Ready\n");
  cr_assert_eq(conn.frames->len, 3);
  _assert_frame(&conn, 0, "abc", 3);
  _assert_frame(&conn, 1, "def", 3);
  _assert_frame(&conn, 2, "ghi", 3);

  _report_durable(&conn, 0);
  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  _report_durable(&conn, 1);
  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  cr_assert_str_eq(conn.replies->str, ALTP_BANNER "250 Received 0\n250 Ready\n",
                   "a Batch must not be acknowledged before all of its Frames are durable");

  _report_durable(&conn, 2);
  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 3\n");

  _connection_deinit(&conn);
}

/* the counters are reset by the next command line, not by the ack (ADR-0004) */
Test(altp, the_counters_are_reset_by_the_command_line_following_an_acknowledgement)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc3 def.\n", -1, LTM_EOF),
                   FALSE);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  _report_durable(&conn, 0);
  _report_durable(&conn, 1);
  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 2\n");

  _inject(&conn, "DATA\n");
  _inject(&conn, "5 world.\n");
  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  _assert_frame(&conn, 2, "world", 5);

  _report_durable(&conn, 2);
  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 2\n"
                                     "250 Ready\n250 Received 1\n");

  _connection_deinit(&conn);
}

Test(altp, a_frame_header_with_leading_zeros_and_a_crlf_terminator_are_accepted)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "003 abc.\r\n", -1, LTM_EOF),
                   FALSE);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  _assert_frame(&conn, 0, "abc", 3);

  _report_durable(&conn, 0);
  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 1\n");

  _connection_deinit(&conn);
}

/* opaque octets, no dot stuffing: Frames are consumed by length (8.2) */
Test(altp, a_binary_payload_is_delivered_intact)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;
  const gchar payload[] = { 'a', 0, '\n', '.', '\n', 'b', '\r', '.' };
  GString *input = g_string_new("8 ");

  g_string_append_len(input, payload, sizeof(payload));
  g_string_append(input, ".\n");

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1,
                                                          input->str, (gssize) input->len, LTM_EOF),
                   FALSE);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  cr_assert_eq(conn.frames->len, 1);
  _assert_frame(&conn, 0, payload, sizeof(payload));

  _connection_deinit(&conn);
  g_string_free(input, TRUE);
}

Test(altp, an_invalid_frame_header_is_answered_501_and_closes)
{
  const gchar *expected = ALTP_BANNER "250 Received 0\n250 Ready\n501 Invalid frame header\n";

  /* command text where a Frame header is expected */
  _assert_conversation("altp()", "SYNC s1\nDATA\nNOOP\n", expected, FALSE);
  _assert_conversation("altp()", "SYNC s1\nDATA\n-3 abc", expected, FALSE);
  _assert_conversation("altp()", "SYNC s1\nDATA\n12345678901 abc", expected, FALSE);
  /* a digit run ended by anything but SP */
  _assert_conversation("altp()", "SYNC s1\nDATA\n3\nabc", expected, FALSE);
  /* an empty payload carries no information */
  _assert_conversation("altp()", "SYNC s1\nDATA\n0 ", expected, FALSE);
  /* neither a Frame header nor a terminator line */
  _assert_conversation("altp()", "SYNC s1\nDATA\n.x\n", expected, FALSE);
}

/* a length above log-msg-size(): the payload is not read at all (8.2, 8.5) */
Test(altp, an_over_sized_frame_is_answered_552_and_closes)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;
  gchar *frame = g_strdup_printf("%d xxx", options_storage.super.max_msg_size + 1);

  _connection_init(&conn, factory,
                   log_transport_mock_records_new("SYNC s1\nDATA\n", -1, frame, -1, LTM_EOF), FALSE);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_EOF);
  cr_assert_str_eq(conn.replies->str, ALTP_BANNER "250 Received 0\n250 Ready\n552 Frame too large\n");
  cr_assert_eq(conn.frames->len, 0, "no Frame may be delivered upstream from an over-sized header");

  _connection_deinit(&conn);
  g_free(frame);
}

/****************************************************************************
 * Resuming an interrupted Batch, and the deferred SYNC reply (7.2, 7.3, 10.3)
 ****************************************************************************/

static void
_interrupt_a_batch_of_three(LogProtoServerFactory *factory, AltpTestConnection *conn)
{
  _connection_init(conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc3 def3 ghi", -1, LTM_EOF),
                   FALSE);

  cr_assert_eq(_pump_until(conn, 0, 3), ALTP_PUMP_FRAMES);
  cr_assert_str_eq(conn->replies->str, ALTP_BANNER "250 Received 0\n250 Ready\n");
  _report_durable(conn, 0);
  _report_durable(conn, 1);
}

Test(altp, a_sync_of_an_interrupted_batch_defers_its_reply_until_the_batch_is_durable)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection first, second;

  _interrupt_a_batch_of_three(factory, &first);

  /* the Sender reconnects and presents the same Session ID */
  _connection_init(&second, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);

  cr_assert_eq(_pump(&second), ALTP_PUMP_SUSPENDED, "the SYNC reply must be deferred");
  cr_assert_str_eq(second.replies->str, ALTP_BANNER);

  /* the report of the older Connection updates the very same Session Record (7.3) */
  _report_durable(&first, 2);
  _assert_replies(&second, ALTP_BANNER "250 Received 3\n");

  _connection_deinit(&second);
  _connection_deinit(&first);
}

/* The acknowledgement timeout expires while the SYNC reply is deferred: the
 * Receiver reports the count durable at that moment (7.2, 9.3). */
Test(altp, the_acknowledgement_timeout_answers_a_deferred_sync_partially)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection first, second;

  _interrupt_a_batch_of_three(factory, &first);

  _connection_init(&second, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  cr_assert_eq(_pump(&second), ALTP_PUMP_SUSPENDED);

  log_proto_altp_server_fire_ack_timeout(second.proto);
  _assert_replies(&second, ALTP_BANNER "250 Received 2\n");

  _connection_deinit(&second);
  _connection_deinit(&first);
}

/* a partial acknowledgement sets frames_read := frames_acked, so the Frames
 * beyond it are disowned and their later durability reports are stale (9.3, 10.1) */
Test(altp, a_partial_acknowledgement_disowns_the_frames_beyond_it)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc3 def3 ghi.\n", -1,
                                                          LTM_EOF),
                   FALSE);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  cr_assert_eq(conn.frames->len, 3);
  _report_durable(&conn, 0);

  log_proto_altp_server_fire_ack_timeout(conn.proto);
  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 1\n");

  /* the Sender opens the Batch that resends the disowned Frames */
  _inject(&conn, "DATA\n");
  _inject(&conn, "5 world.\n");
  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  _assert_frame(&conn, 3, "world", 5);

  /* the disowned Frames complete anyway: their reports refer to a Batch already
   * acknowledged and MUST be ignored, or they corrupt the Batch that is open now */
  _report_durable(&conn, 1);
  _report_durable(&conn, 2);

  _report_durable(&conn, 3);
  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 1\n"
                                     "250 Ready\n250 Received 1\n");

  _connection_deinit(&conn);
}

/* not even before the next Batch opens: a later Connection would resume too far (9.3) */
Test(altp, a_stale_durability_report_does_not_raise_the_acknowledged_count)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection first, second;

  _connection_init(&first, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc3 def3 ghi.\n", -1,
                                                          LTM_EOF),
                   FALSE);

  cr_assert_eq(_pump(&first), ALTP_PUMP_SUSPENDED);
  _report_durable(&first, 0);
  log_proto_altp_server_fire_ack_timeout(first.proto);
  _assert_replies(&first, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 1\n");

  _report_durable(&first, 1);
  _report_durable(&first, 2);

  _connection_init(&second, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  _assert_replies(&second, ALTP_BANNER "250 Received 1\n");

  _connection_deinit(&second);
  _connection_deinit(&first);
}

/* newest connection wins: the older Connection closes without a reply (7.3) */
Test(altp, a_newer_connection_of_a_session_displaces_the_older_one)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection first, second;

  _connection_init(&first, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  _assert_replies(&first, ALTP_BANNER "250 Received 0\n");

  _connection_init(&second, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  _assert_replies(&second, ALTP_BANNER "250 Received 0\n");

  cr_assert_eq(_pump(&first), ALTP_PUMP_EOF, "the displaced Connection has to close");
  cr_assert_str_eq(first.replies->str, ALTP_BANNER "250 Received 0\n");

  /* the Session Record names the peer of whichever Connection owns it now (15) */
  AltpSessionRecord *record =
    altp_session_registry_lookup(altp_receiver_context_get_registry(_get_altp_options()->context), "s1");
  gchar owner_address[MAX_SOCKADDR_STRING] = "";

  cr_assert(altp_session_record_get_owner_peer_address(record, owner_address, sizeof(owner_address)),
            "the Session of a live Connection has to name its peer");
  cr_assert_str_neq(owner_address, "");
  altp_session_record_unref(record);

  _connection_deinit(&second);
  _connection_deinit(&first);
}

/* their durability reports now wake the new Owning Connection (7.3) */
Test(altp, the_frames_of_a_displaced_connection_stay_counted)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection first, second;

  _interrupt_a_batch_of_three(factory, &first);

  _connection_init(&second, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  cr_assert_eq(_pump(&second), ALTP_PUMP_SUSPENDED, "two of the three Frames are durable so far");

  cr_assert_eq(_pump(&first), ALTP_PUMP_EOF, "the displaced Connection closes without a reply");
  cr_assert_str_eq(first.replies->str, ALTP_BANNER "250 Received 0\n250 Ready\n");

  _report_durable(&first, 2);
  _assert_replies(&second, ALTP_BANNER "250 Received 3\n");

  _connection_deinit(&second);
  _connection_deinit(&first);
}

/* A displaced Connection asks for write-only readiness whatever state it was taken
 * over in, so one idling in COMMAND closes without a single octet arriving and
 * without a reply of its own (7.3).  Asking to write rather than for an immediate
 * fetch lets it close while its Frames still hold the window (ADR-0006). */
Test(altp, a_displaced_connection_asks_for_write_only_readiness)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection first, second;
  GIOCondition cond = 0;
  gint timeout = -1;

  _connection_init(&first, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  _assert_replies(&first, ALTP_BANNER "250 Received 0\n");

  _connection_init(&second, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  _assert_replies(&second, ALTP_BANNER "250 Received 0\n");

  cr_assert_eq(log_proto_server_poll_prepare(first.proto, &cond, &timeout), LPPA_POLL_IO);
  cr_assert_eq(cond, G_IO_OUT, "a displaced ALTP Connection must not ask to read");
  cr_assert_eq(timeout, 0, "the idle timeout of a displaced Connection is disarmed");

  const guchar *msg = NULL;
  gsize msg_len = 0;
  gboolean may_read = TRUE;
  Bookmark bookmark;

  memset(&bookmark, 0, sizeof(bookmark));
  cr_assert_eq(log_proto_server_fetch(first.proto, &msg, &msg_len, &may_read, NULL, &bookmark), LPS_EOF);
  cr_assert_null(msg);

  _collect_written(&first);
  cr_assert_str_eq(first.replies->str, ALTP_BANNER "250 Received 0\n",
                   "a displaced Connection closes without a reply of its own");

  _connection_deinit(&second);
  _connection_deinit(&first);
}

/****************************************************************************
 * Waking the Owning Connection (specification 12.1)
 ****************************************************************************/

static void
_count_wakeup(gpointer user_data)
{
  gint *wakeups = (gint *) user_data;

  (*wakeups)++;
}

Test(altp, the_owning_connection_is_woken_once_its_batch_is_durable)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;
  gint wakeups = 0;

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc3 def.\n", -1, LTM_EOF),
                   FALSE);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  log_proto_server_set_wakeup_cb(conn.proto, _count_wakeup, &wakeups);

  _report_durable(&conn, 0);
  cr_assert_eq(wakeups, 0, "a Connection is not woken while its Batch is incomplete");

  _report_durable(&conn, 1);
  cr_assert_eq(wakeups, 1, "the Owning Connection has to be woken once its Batch is durable");

  _connection_deinit(&conn);
}

Test(altp, the_acknowledgement_timeout_wakes_the_owning_connection)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;
  gint wakeups = 0;

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc.\n", -1, LTM_EOF), FALSE);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  log_proto_server_set_wakeup_cb(conn.proto, _count_wakeup, &wakeups);

  log_proto_altp_server_fire_ack_timeout(conn.proto);
  cr_assert_eq(wakeups, 1);

  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 0\n");

  _connection_deinit(&conn);
}

Test(altp, a_displaced_connection_is_woken_by_the_takeover)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection first, second;
  gint wakeups = 0;

  _connection_init(&first, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  _assert_replies(&first, ALTP_BANNER "250 Received 0\n");
  log_proto_server_set_wakeup_cb(first.proto, _count_wakeup, &wakeups);

  _connection_init(&second, factory, log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE);
  _assert_replies(&second, ALTP_BANNER "250 Received 0\n");

  cr_assert_eq(wakeups, 1, "a displaced Connection has to be woken so that it can close");

  _connection_deinit(&second);
  _connection_deinit(&first);
}

/****************************************************************************
 * Bookmarks
 ****************************************************************************/

/* a Bookmark never filled, or filled again, leaks neither a reference nor a report */
Test(altp, a_bookmark_that_a_fetch_did_not_use_is_harmless)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc3 def.\n", -1, LTM_EOF),
                   FALSE);

  /* one single Bookmark for every fetch() of the Connection, used or not */
  conn.shared_bookmark = g_new0(Bookmark, 1);

  cr_assert_eq(_pump(&conn), ALTP_PUMP_SUSPENDED);
  cr_assert_eq(conn.frames->len, 2);

  /* the Registry, the Connection, the Bookmark and the lookup below: an
   * overwritten fill dropped its own reference */
  AltpSessionRecord *record =
    altp_session_registry_lookup(altp_receiver_context_get_registry(_get_altp_options()->context), "s1");

  cr_assert_eq(altp_session_record_get_ref_count(record), 4,
               "a Bookmark that was filled again leaked a Session Record reference");
  altp_session_record_unref(record);

  /* the last fill stands for the second Frame, so saving it ends the Batch */
  bookmark_save(conn.shared_bookmark);
  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 2\n");

  /* saving the very same Bookmark once more reports nothing new */
  bookmark_save(conn.shared_bookmark);
  _connection_deinit(&conn);
}

/****************************************************************************
 * poll_prepare: what the LogReader is told to wait for
 ****************************************************************************/

/* the idle timeout is armed in COMMAND only, as the COMMAND row of 12.1 asks */
Test(altp, the_command_state_arms_the_idle_timeout_of_the_specification)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;
  GIOCondition cond = 0;
  gint timeout = -1;

  _connection_init(&conn, factory, log_transport_mock_endless_stream_new("", 0, LTM_EOF), FALSE);

  /* the banner is not written yet, so all we want is to write */
  cr_assert_eq(log_proto_server_poll_prepare(conn.proto, &cond, &timeout), LPPA_POLL_IO);
  cr_assert_eq(cond, G_IO_OUT);
  cr_assert_eq(timeout, 0);

  _assert_replies(&conn, ALTP_BANNER);

  cond = 0;
  timeout = -1;
  cr_assert_eq(log_proto_server_poll_prepare(conn.proto, &cond, &timeout), LPPA_POLL_IO);
  cr_assert_eq(cond, G_IO_IN);
  cr_assert_eq(timeout, 60, "the default idle timeout of an ALTP Receiver is 60 seconds");

  _connection_deinit(&conn);
}

Test(altp, the_idle_timeout_is_not_armed_while_a_batch_is_open)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;
  GIOCondition cond = 0;
  gint timeout = -1;

  _connection_init(&conn, factory,
                   log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, LTM_EOF), FALSE);

  _assert_replies(&conn, ALTP_BANNER "250 Received 0\n250 Ready\n");

  cr_assert_eq(log_proto_server_poll_prepare(conn.proto, &cond, &timeout), LPPA_POLL_IO);
  cr_assert_eq(cond, G_IO_IN);
  cr_assert_eq(timeout, 0);

  _connection_deinit(&conn);
}

/****************************************************************************
 * Persistence: the Session Records of a Receiver outlive its process (10.1)
 ****************************************************************************/

/* every persistent entry name of the Receiver is prefixed with this */
#define ALTP_TEST_PERSIST_RECEIVER "s_altp_persist#0"

static gchar *
_entry_name(const gchar *session_id)
{
  return g_strdup_printf("%s.altp.session(%s)", ALTP_TEST_PERSIST_RECEIVER, session_id);
}

/* what a Connection does through restart_with_state() */
static AltpSessionRegistry *
_bind_registry_full(PersistState *state, gint session_expiration, gint max_sessions)
{
  AltpSessionRegistry *registry = altp_session_registry_ref_by_name(ALTP_TEST_PERSIST_RECEIVER);

  altp_session_registry_bind_persist_state(registry, state, session_expiration, max_sessions);
  return registry;
}

static AltpSessionRegistry *
_bind_registry(PersistState *state, gint session_expiration)
{
  return _bind_registry_full(state, session_expiration, ALTP_DEFAULT_MAX_SESSIONS);
}

/* deliver @count Frames upstream and report the first @durable of them durable */
static void
_drive_record(AltpSessionRecord *record, guint count, guint durable)
{
  Bookmark bookmarks[8];

  cr_assert_leq(count, (guint) G_N_ELEMENTS(bookmarks));
  memset(bookmarks, 0, sizeof(bookmarks));

  for (guint i = 0; i < count; i++)
    {
      guint64 batch_seq;
      guint32 frame_index;

      altp_session_record_note_frame_read(record, &batch_seq, &frame_index);
      altp_session_bookmark_fill(&bookmarks[i], record, batch_seq, frame_index);
    }

  /* the consecutive ack tracker saves the last Frame of the durable prefix only */
  if (durable > 0)
    bookmark_save(&bookmarks[durable - 1]);

  for (guint i = 0; i < count; i++)
    bookmark_destroy(&bookmarks[i]);
}

static void
_assert_counters(AltpSessionRecord *record, guint32 expected_read, guint32 expected_acked)
{
  guint32 frames_read = 0, frames_acked = 0;

  altp_session_record_get_counters(record, &frames_read, &frames_acked);
  cr_assert_eq(frames_read, expected_read, "frames_read is %u, not %u", frames_read, expected_read);
  cr_assert_eq(frames_acked, expected_acked, "frames_acked is %u, not %u", frames_acked, expected_acked);
}

Test(altp, the_counters_of_a_session_survive_a_restart_of_the_receiver)
{
  PersistState *state = clean_and_create_persist_state_for_test("test_altp_counters.persist");
  AltpSessionRegistry *registry = _bind_registry(state, 3600);

  AltpSessionRecord *record = altp_session_registry_lookup(registry, "s1");
  _drive_record(record, 3, 3);
  _assert_counters(record, 3, 3);

  /* the Receiver stops and the persistent state is written out */
  altp_session_record_unref(record);
  altp_session_registry_unref(registry);
  state = restart_persist_state(state);

  registry = _bind_registry(state, 3600);
  cr_assert_eq(altp_session_registry_get_session_count(registry), 0,
               "a persisted Session Record is loaded by its first lookup, not by binding the registry");

  record = altp_session_registry_lookup(registry, "s1");
  _assert_counters(record, 3, 3);
  cr_assert_eq(altp_session_registry_get_session_count(registry), 1);

  altp_session_record_unref(record);
  altp_session_registry_unref(registry);
  commit_and_destroy_persist_state(state);
}

/* on restart frames_read := frames_acked: what was read but not durable when the
 * process stopped was in memory only (10.1) */
Test(altp, the_restart_rule_rewinds_frames_read_to_the_durable_prefix)
{
  PersistState *state = clean_and_create_persist_state_for_test("test_altp_restart_rule.persist");
  AltpSessionRegistry *registry = _bind_registry(state, 3600);

  AltpSessionRecord *record = altp_session_registry_lookup(registry, "s1");
  _drive_record(record, 3, 2);
  _assert_counters(record, 3, 2);

  altp_session_record_unref(record);
  altp_session_registry_unref(registry);
  state = restart_persist_state(state);

  registry = _bind_registry(state, 3600);
  record = altp_session_registry_lookup(registry, "s1");
  _assert_counters(record, 2, 2);

  altp_session_record_unref(record);
  altp_session_registry_unref(registry);
  commit_and_destroy_persist_state(state);
}

Test(altp, the_restart_sweep_forgets_a_session_unused_for_longer_than_session_expiration)
{
  fake_time(1600000000);

  PersistState *state = clean_and_create_persist_state_for_test("test_altp_expiry.persist");
  AltpSessionRegistry *registry = _bind_registry(state, 100);

  AltpSessionRecord *record = altp_session_registry_lookup(registry, "s_old");
  _drive_record(record, 2, 2);
  altp_session_record_unref(record);

  fake_time_add(200);

  record = altp_session_registry_lookup(registry, "s_fresh");
  _drive_record(record, 4, 4);
  altp_session_record_unref(record);
  altp_session_registry_unref(registry);

  state = restart_persist_state(state);
  registry = _bind_registry(state, 100);

  record = altp_session_registry_lookup(registry, "s_old");
  _assert_counters(record, 0, 0);
  altp_session_record_unref(record);

  record = altp_session_registry_lookup(registry, "s_fresh");
  _assert_counters(record, 4, 4);
  altp_session_record_unref(record);

  altp_session_registry_unref(registry);
  commit_and_destroy_persist_state(state);
}

/* the Session starts over, which 10.1 allows at the price of duplicates */
Test(altp, a_persisted_session_record_of_an_unknown_version_is_refused)
{
  PersistState *state = clean_and_create_persist_state_for_test("test_altp_version.persist");
  AltpSessionRegistry *registry = _bind_registry(state, 3600);
  gchar *name = _entry_name("s1");

  /* an entry of our size but of a version that is not ours */
  PersistEntryHandle handle = persist_state_alloc_entry(state, name, 24);
  guint8 *raw = (guint8 *) persist_state_map_entry(state, handle);
  memset(raw, 0, 24);
  raw[0] = 42;
  persist_state_unmap_entry(state, handle);

  start_grabbing_messages();
  AltpSessionRecord *record = altp_session_registry_lookup(registry, "s1");
  stop_grabbing_messages();

  assert_grabbed_log_contains("The persisted ALTP Session Record has an unknown version");
  _assert_counters(record, 0, 0);

  _drive_record(record, 2, 2);
  altp_session_record_unref(record);
  altp_session_registry_unref(registry);
  state = restart_persist_state(state);

  registry = _bind_registry(state, 3600);
  record = altp_session_registry_lookup(registry, "s1");
  _assert_counters(record, 2, 2);

  altp_session_record_unref(record);
  altp_session_registry_unref(registry);
  g_free(name);
  commit_and_destroy_persist_state(state);
}

Test(altp, a_persisted_session_record_of_the_wrong_size_is_refused)
{
  PersistState *state = clean_and_create_persist_state_for_test("test_altp_size.persist");
  AltpSessionRegistry *registry = _bind_registry(state, 3600);
  gchar *name = _entry_name("s1");

  PersistEntryHandle handle = persist_state_alloc_entry(state, name, 8);
  guint8 *raw = (guint8 *) persist_state_map_entry(state, handle);
  memset(raw, 0, 8);
  raw[0] = ALTP_SESSION_PERSIST_VERSION;
  persist_state_unmap_entry(state, handle);

  start_grabbing_messages();
  AltpSessionRecord *record = altp_session_registry_lookup(registry, "s1");
  stop_grabbing_messages();

  assert_grabbed_log_contains("The persisted ALTP Session Record has an unexpected size");
  _assert_counters(record, 0, 0);

  altp_session_record_unref(record);
  altp_session_registry_unref(registry);
  g_free(name);
  cancel_and_destroy_persist_state(state);
}

Test(altp, the_expiry_sweep_drops_an_unused_session_record_and_keeps_a_held_one)
{
  fake_time(1600000000);

  PersistState *state = clean_and_create_persist_state_for_test("test_altp_sweep.persist");
  AltpSessionRegistry *registry = _bind_registry(state, 100);

  AltpSessionRecord *unused = altp_session_registry_lookup(registry, "s_unused");
  _drive_record(unused, 2, 2);
  altp_session_record_unref(unused);

  AltpSessionRecord *held = altp_session_registry_lookup(registry, "s_held");
  _drive_record(held, 3, 3);

  cr_assert_eq(altp_session_registry_get_session_count(registry), 2);

  fake_time_add(200);
  altp_session_registry_fire_expiry(registry);

  cr_assert_eq(altp_session_registry_get_session_count(registry), 1,
               "the Session Record a Connection still holds may not be expired");
  cr_assert_eq(atomic_gssize_get(altp_session_registry_get_session_count_ref(registry)), 1,
               "the altp_sessions gauge has to follow the registry");
  _assert_counters(held, 3, 3);

  /* the persisted entry went with it, so the Session comes back unknown (10.1) */
  AltpSessionRecord *record = altp_session_registry_lookup(registry, "s_unused");
  _assert_counters(record, 0, 0);

  altp_session_record_unref(record);
  altp_session_record_unref(held);
  altp_session_registry_unref(registry);
  commit_and_destroy_persist_state(state);
}

/* the registry is per persistent name but session-expiration() is per driver */
Test(altp, two_receivers_of_one_registry_share_the_smaller_session_expiration)
{
  fake_time(1600000000);

  PersistState *state = clean_and_create_persist_state_for_test("test_altp_rebind.persist");
  AltpSessionRegistry *registry = _bind_registry(state, 1000);

  /* the second Receiver asks for a longer expiration and does not get it */
  start_grabbing_messages();
  AltpSessionRegistry *again = altp_session_registry_ref_by_name(ALTP_TEST_PERSIST_RECEIVER);
  altp_session_registry_bind_persist_state(again, state, 5000, ALTP_DEFAULT_MAX_SESSIONS);
  stop_grabbing_messages();

  cr_assert_eq(again, registry, "a Session Registry is keyed by the persistent name of the Receiver");
  assert_grabbed_log_contains("the smaller value stays in effect");

  AltpSessionRecord *record = altp_session_registry_lookup(registry, "s1");
  altp_session_record_unref(record);

  fake_time_add(2000);
  altp_session_registry_fire_expiry(registry);
  cr_assert_eq(altp_session_registry_get_session_count(registry), 0,
               "the 1000 seconds of the first Receiver apply, not the 5000 of the second");

  /* while a third one asking for less does lower it */
  start_grabbing_messages();
  altp_session_registry_bind_persist_state(registry, state, 10, ALTP_DEFAULT_MAX_SESSIONS);
  stop_grabbing_messages();
  assert_grabbed_log_contains("the smaller value applies");

  altp_session_registry_unref(again);
  altp_session_registry_unref(registry);
  commit_and_destroy_persist_state(state);
}

/****************************************************************************
 * max-sessions(): the number of Session Records a Receiver keeps is bounded (15)
 ****************************************************************************/

/* A Session Record is created on the word of an unauthenticated peer, since the
 * Sender picks the Session ID, so their number is bounded.  An established Session
 * is never evicted to make room, or churning Sessions would be a way to destroy
 * the state of the legitimate Senders of the Receiver. */
Test(altp, a_new_session_beyond_max_sessions_gets_no_session_record)
{
  fake_time(1600000000);

  PersistState *state = clean_and_create_persist_state_for_test("test_altp_max_sessions.persist");
  AltpSessionRegistry *registry = _bind_registry_full(state, 3600, 2);

  AltpSessionRecord *first = altp_session_registry_lookup(registry, "s1");
  AltpSessionRecord *second = altp_session_registry_lookup(registry, "s2");

  cr_assert_not_null(first);
  cr_assert_not_null(second);
  cr_assert_eq(altp_session_registry_get_session_count(registry), 2);

  cr_assert_null(altp_session_registry_lookup(registry, "s3"),
                 "a Session beyond max-sessions() may not be created");
  cr_assert_eq(altp_session_registry_get_session_count(registry), 2);

  AltpSessionRecord *again = altp_session_registry_lookup(registry, "s1");

  cr_assert_eq(again, first, "a Session that has a Session Record is served at the bound");
  altp_session_record_unref(again);

  altp_session_record_unref(second);
  fake_time_add(4000);
  altp_session_registry_fire_expiry(registry);
  cr_assert_eq(altp_session_registry_get_session_count(registry), 1);

  AltpSessionRecord *third = altp_session_registry_lookup(registry, "s3");

  cr_assert_not_null(third);
  altp_session_record_unref(third);

  altp_session_record_unref(first);
  altp_session_registry_unref(registry);
  commit_and_destroy_persist_state(state);
}

Test(altp, max_sessions_zero_admits_any_number_of_sessions)
{
  PersistState *state = clean_and_create_persist_state_for_test("test_altp_unlimited.persist");
  AltpSessionRegistry *registry = _bind_registry_full(state, 3600, 0);

  for (gint i = 0; i < 8; i++)
    {
      gchar *session_id = g_strdup_printf("s%d", i);
      AltpSessionRecord *record = altp_session_registry_lookup(registry, session_id);

      cr_assert_not_null(record, "max-sessions(0) may not refuse a Session");
      altp_session_record_unref(record);
      g_free(session_id);
    }

  cr_assert_eq(altp_session_registry_get_session_count(registry), 8);

  altp_session_registry_unref(registry);
  commit_and_destroy_persist_state(state);
}

/****************************************************************************
 * A configuration reload: the options are recreated while the Connections live on (ADR-0005)
 ****************************************************************************/

/* afsocket keeps the Connections of a driver across a reload and reinstalls them
 * without constructing a proto, so the proto has to survive the options -- and the
 * receiver context -- it was born in, and the new context has to find the very same
 * Session Registry under the very same persistent name (ADR-0005). */
Test(altp, a_connection_kept_across_a_configuration_reload_keeps_its_session)
{
  LogProtoServerOptionsStorage old_storage, new_storage;
  AltpTestConnection first, second;

  /* the old configuration: a Connection mid-Batch, two of its three Frames durable */
  LogProtoServerFactory *old_factory = _parse_altp_transport_into(&old_storage, "altp()");

  LogTransport *transport =
    log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc3 def3 ghi", -1, LTM_EOF);

  _connection_init_full(&first, old_factory, &old_storage, transport, FALSE, NULL, ALTP_TEST_PERSIST_NAME, NULL);

  cr_assert_eq(_pump_until(&first, 0, 3), ALTP_PUMP_FRAMES);
  cr_assert_str_eq(first.replies->str, ALTP_BANNER "250 Received 0\n250 Ready\n");
  _report_durable(&first, 0);
  _report_durable(&first, 1);

  /* the reload: the old options are destroyed under the Connection that survived */
  LogProtoServerFactory *new_factory = _parse_altp_transport_into(&new_storage, "altp()");
  log_proto_server_options_destroy(&old_storage.super);

  _inject(&first, ".\n");
  cr_assert_eq(_pump(&first), ALTP_PUMP_SUSPENDED,
               "the Connection of the old configuration lost its Session Record");

  /* a Connection of the new configuration finds the same Session Record (7.2, 7.3) */
  _connection_init_full(&second, new_factory, &new_storage,
                        log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE,
                        NULL, ALTP_TEST_PERSIST_NAME, NULL);

  cr_assert_eq(_pump(&second), ALTP_PUMP_SUSPENDED, "the SYNC reply must be deferred");
  cr_assert_str_eq(second.replies->str, ALTP_BANNER);

  cr_assert_eq(_pump(&first), ALTP_PUMP_EOF, "the displaced Connection has to close");

  _report_durable(&first, 2);
  _assert_replies(&second, ALTP_BANNER "250 Received 3\n");

  _connection_deinit(&second);
  _connection_deinit(&first);
  log_proto_server_options_destroy(&new_storage.super);
}

/****************************************************************************
 * The metrics of one Receiver (no per Session labels)
 ****************************************************************************/

static gsize
_get_metric(const gchar *name, const gchar *label, const gchar *value)
{
  StatsClusterKeyBuilder *kb = stats_cluster_key_builder_new();

  stats_cluster_key_builder_set_name(kb, name);
  if (label)
    stats_cluster_key_builder_add_label(kb, stats_cluster_label(label, value));

  StatsClusterKey *key = stats_cluster_key_builder_build_single(kb);
  StatsCounterItem *counter = NULL;
  gsize result = 0;

  stats_lock();
  {
    /* registering the very same key reaches the counter the Connections share */
    cr_assert_not_null(stats_register_counter(STATS_LEVEL1, key, SC_TYPE_SINGLE_VALUE, &counter));
    result = stats_counter_get(counter);
    stats_unregister_counter(key, SC_TYPE_SINGLE_VALUE, &counter);
  }
  stats_unlock();

  stats_cluster_key_free(key);
  stats_cluster_key_builder_free(kb);

  return result;
}

static void
_enable_stats(void)
{
  configuration->stats_options.level = STATS_LEVEL1;
  stats_reinit(&configuration->stats_options);
}

Test(altp, the_receiver_metrics_count_acknowledgements_takeovers_and_refusals)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  StatsClusterKeyBuilder *kb = stats_cluster_key_builder_new();
  AltpTestConnection first, second, third;

  _enable_stats();

  /* one acknowledgement for the SYNC, one for the Batch of two Frames */
  LogTransport *transport =
    log_transport_mock_endless_records_new("SYNC s1\nDATA\n", -1, "3 abc3 def.\n", -1, LTM_EOF);

  _connection_init_full(&first, factory, &options_storage, transport, FALSE, NULL, ALTP_TEST_PERSIST_NAME, kb);

  cr_assert_eq(altp_session_registry_get_session_count(
                 altp_receiver_context_get_registry(_get_altp_options()->context)), 0);
  cr_assert_eq(_get_metric(METRIC(altp_sessions), NULL, NULL), 0);

  cr_assert_eq(_pump(&first), ALTP_PUMP_SUSPENDED);
  _report_durable(&first, 0);
  _report_durable(&first, 1);
  _assert_replies(&first, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 2\n");

  cr_assert_eq(_get_metric(METRIC(altp_acknowledgements_total), "result", "complete"), 2);
  cr_assert_eq(_get_metric(METRIC(altp_acknowledgements_total), "result", "partial"), 0);
  cr_assert_eq(_get_metric(METRIC(altp_sessions), NULL, NULL), 1,
               "the altp_sessions gauge is the number of Session Records of the Receiver");

  /* a Batch whose acknowledgement timeout expires is acknowledged partially */
  _inject(&first, "DATA\n");
  _inject(&first, "5 world.\n");
  cr_assert_eq(_pump(&first), ALTP_PUMP_SUSPENDED);
  log_proto_altp_server_fire_ack_timeout(first.proto);
  _assert_replies(&first, ALTP_BANNER "250 Received 0\n250 Ready\n250 Received 2\n"
                                      "250 Ready\n250 Received 0\n");

  cr_assert_eq(_get_metric(METRIC(altp_acknowledgements_total), "result", "partial"), 1);

  /* a newer Connection of the Session takes it over */
  _connection_init_full(&second, factory, &options_storage,
                        log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE,
                        NULL, ALTP_TEST_PERSIST_NAME, kb);
  _assert_replies(&second, ALTP_BANNER "250 Received 0\n");

  cr_assert_eq(_get_metric(METRIC(altp_session_takeovers_total), NULL, NULL), 1);

  /* and a 5xx refusal is counted under the code it was answered with */
  cr_assert_eq(_get_metric(METRIC(altp_replies_total), "code", "503"), 0);

  _connection_init_full(&third, factory, &options_storage,
                        log_transport_mock_stream_new("DATA\n", -1, LTM_EOF), FALSE,
                        NULL, ALTP_TEST_PERSIST_NAME, kb);
  cr_assert_eq(_pump(&third), ALTP_PUMP_EOF);
  cr_assert_str_eq(third.replies->str, ALTP_BANNER "503 Need SYNC before use this command\n");

  cr_assert_eq(_get_metric(METRIC(altp_replies_total), "code", "503"), 1);
  cr_assert_eq(_get_metric(METRIC(altp_replies_total), "code", "504"), 0,
               "only the codes the Receiver actually sent may appear");

  _connection_deinit(&third);
  _connection_deinit(&second);
  _connection_deinit(&first);
  stats_cluster_key_builder_free(kb);
}

/* 1.0 defines no reply code for a refused Session, so it just closes (4.3, 14.2) */
Test(altp, a_connection_refused_at_max_sessions_closes_without_a_reply)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp(max-sessions(2))");
  StatsClusterKeyBuilder *kb = stats_cluster_key_builder_new();
  AltpTestConnection first, second, third, fourth;

  _enable_stats();

  _connection_init_full(&first, factory, &options_storage,
                        log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE,
                        NULL, ALTP_TEST_PERSIST_NAME, kb);
  _assert_replies(&first, ALTP_BANNER "250 Received 0\n");

  _connection_init_full(&second, factory, &options_storage,
                        log_transport_mock_endless_stream_new("SYNC s2\n", -1, LTM_EOF), FALSE,
                        NULL, ALTP_TEST_PERSIST_NAME, kb);
  _assert_replies(&second, ALTP_BANNER "250 Received 0\n");

  cr_assert_eq(_get_metric(METRIC(altp_sessions), NULL, NULL), 2);
  cr_assert_eq(_get_metric(METRIC(altp_sessions_refused_total), NULL, NULL), 0);

  start_grabbing_messages();
  _connection_init_full(&third, factory, &options_storage,
                        log_transport_mock_stream_new("SYNC s3\n", -1, LTM_EOF), FALSE,
                        NULL, ALTP_TEST_PERSIST_NAME, kb);
  cr_assert_eq(_pump(&third), ALTP_PUMP_EOF, "a refused Connection has to close");
  stop_grabbing_messages();

  cr_assert_str_eq(third.replies->str, ALTP_BANNER,
                   "a Session refused at max-sessions() draws no reply at all");
  assert_grabbed_log_contains("Refusing a new ALTP Session");
  cr_assert_eq(_get_metric(METRIC(altp_sessions_refused_total), NULL, NULL), 1);
  cr_assert_eq(_get_metric(METRIC(altp_sessions), NULL, NULL), 2);

  /* a Session the Receiver already holds is served at the bound (7.3) */
  _connection_init_full(&fourth, factory, &options_storage,
                        log_transport_mock_endless_stream_new("SYNC s1\n", -1, LTM_EOF), FALSE,
                        NULL, ALTP_TEST_PERSIST_NAME, kb);
  _assert_replies(&fourth, ALTP_BANNER "250 Received 0\n");

  cr_assert_eq(_get_metric(METRIC(altp_sessions_refused_total), NULL, NULL), 1);

  _connection_deinit(&fourth);
  _connection_deinit(&third);
  _connection_deinit(&second);
  _connection_deinit(&first);
  stats_cluster_key_builder_free(kb);
}

/****************************************************************************
 * CLOSED: a displaced Connection has to close even with an exhausted window
 ****************************************************************************/

/* an immediate fetch is refused while the flow-control window is exhausted, and a
 * displaced Connection keeps its window exhausted (ADR-0006) */
Test(altp, a_closing_connection_asks_for_write_only_readiness)
{
  LogProtoServerFactory *factory = _parse_altp_transport("altp()");
  AltpTestConnection conn;
  GIOCondition cond = 0;
  gint timeout = -1;

  _connection_init(&conn, factory, log_transport_mock_endless_stream_new("ZLIB\n", -1, LTM_EOF), FALSE);

  _assert_replies(&conn, ALTP_BANNER "502 Unknown command\n");

  cond = 0;
  timeout = -1;
  cr_assert_eq(log_proto_server_poll_prepare(conn.proto, &cond, &timeout), LPPA_POLL_IO);
  cr_assert_eq(cond, G_IO_OUT, "a closing ALTP Connection must not ask to read");
  cr_assert_eq(timeout, 0, "the idle timeout is armed in COMMAND only");

  const guchar *msg = NULL;
  gsize msg_len = 0;
  gboolean may_read = TRUE;
  Bookmark bookmark;

  memset(&bookmark, 0, sizeof(bookmark));
  cr_assert_eq(log_proto_server_fetch(conn.proto, &msg, &msg_len, &may_read, NULL, &bookmark), LPS_EOF);
  cr_assert_null(msg);

  _connection_deinit(&conn);
}

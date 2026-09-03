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
#include "libtest/mock-transport.h"
#include "libtest/proto_lib.h"

#include "altp-proto-options.h"
#include "logproto-altp-server.h"

#include "apphook.h"
#include "cfg.h"
#include "driver.h"
#include "plugin.h"
#include "transport/transport-stack.h"

#include <string.h>

#define ALTP_BANNER "220 ALTP 1.0\n"

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
 * A fake TLS transport factory, so that the tests can tell a Connection that
 * has tls() configured from one that has not (see also
 * lib/transport/tests/test_transport_stack.c).
 ****************************************************************************/

static gssize
_fake_tls_read(LogTransport *s, gpointer buf, gsize count, LogTransportAuxData *aux)
{
  return 0;
}

static gssize
_fake_tls_write(LogTransport *s, const gpointer buf, gsize count)
{
  return count;
}

static LogTransport *
_fake_tls_transport_construct(const LogTransportFactory *s, LogTransportStack *stack)
{
  LogTransport *self = g_new0(LogTransport, 1);

  log_transport_init_instance(self, "fake-tls", stack->fd);
  self->read = _fake_tls_read;
  self->write = _fake_tls_write;
  return self;
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
 * Driving a proto the way the LogReader does
 ****************************************************************************/

static LogProtoServerFactory *
_parse_altp_transport(const gchar *config_snippet)
{
  gpointer result = NULL;

  memset(&options_storage, 0, sizeof(options_storage));
  log_proto_server_options_defaults(&options_storage.super);

  cr_assert(parse_config(config_snippet, LL_CONTEXT_SERVER_PROTO, &options_storage.super, &result),
            "cannot parse transport(%s)", config_snippet);
  cr_assert_not_null(result, "the altp grammar did not return a LogProtoServerFactory");

  log_proto_server_options_init(&options_storage.super, configuration);
  options_initialized = TRUE;

  return (LogProtoServerFactory *) result;
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

static LogProtoServer *
_construct_altp_proto(const gchar *config_snippet, LogTransport *transport)
{
  LogProtoServerFactory *factory = _parse_altp_transport(config_snippet);

  return log_proto_server_factory_construct(factory, transport, &options_storage.super, NULL);
}

static void
_collect_written(LogTransport *transport, GString *replies)
{
  gchar buffer[4096];
  gssize len;

  while ((len = log_transport_mock_read_from_write_buffer((LogTransportMock *) transport, buffer,
                                                          sizeof(buffer))) > 0)
    g_string_append_len(replies, buffer, len);
}

/* Ask the proto what it wants and run fetch(), collecting whatever it wrote,
 * until it reports something other than success -- with a mock transport that
 * ends in LTM_EOF that is always the LPS_EOF of the exhausted input or the
 * LPS_EOF of the CLOSED state. */
static LogProtoStatus
_pump_proto(LogProtoServer *proto, LogTransport *transport, GString *replies)
{
  for (gint round = 0; round < 4096; round++)
    {
      GIOCondition cond = 0;
      gint timeout = -1;
      const guchar *msg = NULL;
      gsize msg_len = 0;
      gboolean may_read = TRUE;
      Bookmark bookmark;
      LogTransportAuxData aux;
      LogProtoStatus status;

      memset(&bookmark, 0, sizeof(bookmark));
      memset(&aux, 0, sizeof(aux));

      log_proto_server_poll_prepare(proto, &cond, &timeout);

      log_transport_aux_data_init(&aux);
      status = log_proto_server_fetch(proto, &msg, &msg_len, &may_read, &aux, &bookmark);
      log_transport_aux_data_destroy(&aux);

      cr_assert_null(msg, "the ALTP proto must not return a message before DATA is implemented");

      _collect_written(transport, replies);

      if (status != LPS_SUCCESS && status != LPS_AGAIN)
        return status;
    }

  cr_assert_fail("the ALTP proto never reached a terminal status");
  return LPS_ERROR;
}

static void
_assert_conversation(const gchar *config_snippet, const gchar *input, const gchar *expected_replies,
                     LogProtoStatus expected_status, gboolean with_tls)
{
  LogTransport *transport = log_transport_mock_stream_new(input, -1, LTM_EOF);
  LogProtoServer *proto = _construct_altp_proto(config_snippet, transport);
  GString *replies = g_string_new("");

  if (with_tls)
    log_transport_stack_add_factory(&proto->transport_stack, _fake_tls_transport_factory_new());

  LogProtoStatus status = _pump_proto(proto, transport, replies);

  cr_assert_str_eq(replies->str, expected_replies);
  cr_assert_eq(status, expected_status, "unexpected final LogProtoStatus %d", status);

  g_string_free(replies, TRUE);
  log_proto_server_free(proto);
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
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_AUTO);
}

Test(altp, the_full_option_block_is_parsed)
{
  _parse_altp_transport("altp(ack-timeout(120) session-expiration(3600) tls-policy(required))");

  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 120);
  cr_assert_eq(_get_altp_options()->altp.session_expiration, 3600);
  cr_assert_eq(_get_altp_options()->altp.tls_policy, ALTP_TLS_POLICY_REQUIRED);
}

Test(altp, transport_altp_without_an_option_block_means_all_defaults)
{
  LogProtoServerFactory *factory = _parse_altp_transport_tail(")");

  cr_assert_eq(factory->default_inet_port, 35514);
  cr_assert_eq(_get_altp_options()->altp.ack_timeout, 900);
  cr_assert_eq(_get_altp_options()->altp.session_expiration, 2592000);
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

Test(altp, a_stateful_proto_accepts_the_persistent_state_of_the_driver)
{
  LogTransport *transport = log_transport_mock_stream_new("", 0, LTM_EOF);
  LogProtoServer *proto = _construct_altp_proto("altp()", transport);

  /* afsocket refuses the Connection when this returns FALSE */
  cr_assert(log_proto_server_restart_with_state(proto, NULL, "s_altp#0"));

  log_proto_server_free(proto);
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
                                "tls-policy(required))) "
                                "log-msg-size(65536) idle-timeout(60))");
}

/****************************************************************************
 * The Connection: banner, EHLO, NOOP and the errors of Stage A
 ****************************************************************************/

Test(altp, the_banner_is_written_before_any_command_is_read)
{
  _assert_conversation("altp()", "", ALTP_BANNER, LPS_EOF, FALSE);
}

Test(altp, noop_is_answered_with_250_ok)
{
  _assert_conversation("altp()", "NOOP\n", ALTP_BANNER "250 OK\n", LPS_EOF, FALSE);
}

Test(altp, noop_parameters_are_ignored_rather_than_rejected)
{
  _assert_conversation("altp()", "NOOP keep me alive\n", ALTP_BANNER "250 OK\n", LPS_EOF, FALSE);
}

Test(altp, a_bare_ehlo_means_the_1_0_dialect)
{
  _assert_conversation("altp()", "EHLO\n", ALTP_BANNER "250 \n", LPS_EOF, FALSE);
}

Test(altp, ehlo_with_a_trailing_space_and_an_empty_version_means_1_0)
{
  _assert_conversation("altp()", "EHLO \n", ALTP_BANNER "250 \n", LPS_EOF, FALSE);
}

Test(altp, ehlo_1_0_without_tls_advertises_no_capability)
{
  _assert_conversation("altp()", "EHLO 1.0\n", ALTP_BANNER "250 \n", LPS_EOF, FALSE);
}

Test(altp, ehlo_advertises_starttls_when_the_driver_configured_tls)
{
  _assert_conversation("altp()", "EHLO 1.0\n", ALTP_BANNER "250 STARTTLS\n", LPS_EOF, TRUE);
}

Test(altp, a_bare_ehlo_advertises_starttls_too)
{
  _assert_conversation("altp()", "EHLO\n", ALTP_BANNER "250 STARTTLS\n", LPS_EOF, TRUE);
}

Test(altp, ehlo_may_be_repeated_and_each_reply_supersedes_the_previous_one)
{
  _assert_conversation("altp()", "EHLO 1.0\nEHLO\n", ALTP_BANNER "250 \n250 \n", LPS_EOF, FALSE);
}

Test(altp, an_unsupported_ehlo_version_is_answered_510_and_closes)
{
  _assert_conversation("altp()", "EHLO 2.0\n", ALTP_BANNER "510 Invalid version of dialect\n", LPS_EOF, FALSE);
}

Test(altp, a_malformed_ehlo_version_is_answered_501_and_closes)
{
  _assert_conversation("altp()", "EHLO 1.x\n", ALTP_BANNER "501 Syntax error\n", LPS_EOF, FALSE);
}

Test(altp, an_unknown_verb_is_answered_502_and_closes)
{
  _assert_conversation("altp()", "BOGUS\n", ALTP_BANNER "502 Unknown command\n", LPS_EOF, FALSE);
}

Test(altp, a_token_that_merely_begins_with_a_verb_is_not_accepted)
{
  _assert_conversation("altp()", "NOOPS\n", ALTP_BANNER "502 Unknown command\n", LPS_EOF, FALSE);
}

Test(altp, nothing_is_read_after_a_5xx_reply)
{
  _assert_conversation("altp()", "BOGUS\nNOOP\n", ALTP_BANNER "502 Unknown command\n", LPS_EOF, FALSE);
}

Test(altp, crlf_line_endings_are_accepted)
{
  _assert_conversation("altp()", "NOOP\r\nEHLO 1.0\r\n", ALTP_BANNER "250 OK\n250 \n", LPS_EOF, FALSE);
}

Test(altp, an_over_long_command_line_is_answered_501_and_closes)
{
  GString *input = g_string_new("NOOP ");

  /* the limit counts the terminator, so this line plus its LF is one octet too long */
  while (input->len < ALTP_MAX_COMMAND_LINE)
    g_string_append_c(input, 'x');
  g_string_append_c(input, '\n');

  _assert_conversation("altp()", input->str, ALTP_BANNER "501 Syntax error\n", LPS_EOF, FALSE);
  g_string_free(input, TRUE);
}

Test(altp, a_command_line_of_exactly_the_limit_is_accepted)
{
  GString *input = g_string_new("NOOP ");

  while (input->len < ALTP_MAX_COMMAND_LINE - 1)
    g_string_append_c(input, 'x');
  g_string_append_c(input, '\n');
  cr_assert_eq(input->len, (gsize) ALTP_MAX_COMMAND_LINE);

  _assert_conversation("altp()", input->str, ALTP_BANNER "250 OK\n", LPS_EOF, FALSE);
  g_string_free(input, TRUE);
}

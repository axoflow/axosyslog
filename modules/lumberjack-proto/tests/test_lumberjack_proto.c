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
#include "libtest/proto_lib.h"

#include "lumberjack-proto-options.h"
#include "logproto-lumberjack-server.h"

#include "ack-tracker/ack_tracker_factory.h"
#include "apphook.h"
#include "cfg.h"
#include "cfg-parser.h"
#include "plugin.h"
#include "transport/transport-stack.h"

#include <string.h>

/* The lumberjack options only fit into a LogProtoServerOptionsStorage union,
 * unlike the bare LogProtoServerOptions of proto_lib.h, so the tests own one. */
static LogProtoServerOptionsStorage options_storage;
static gboolean options_initialized;

static void
setup(void)
{
  app_startup();
  init_proto_tests();
  cr_assert(cfg_load_module(configuration, "lumberjack_proto"), "cannot load the lumberjack_proto module");
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

TestSuite(lumberjack, .init = setup, .fini = teardown);

/****************************************************************************
 * Grammar
 ****************************************************************************/

/* Parse @snippet the way afsocket does inside transport(...): the grammar
 * fills the storage and hands back the factory. */
static LogProtoServerFactory *
_parse_lumberjack_transport_into(LogProtoServerOptionsStorage *storage, const gchar *snippet)
{
  LogProtoServerFactory *result = NULL;

  memset(storage, 0, sizeof(*storage));
  log_proto_server_options_defaults(&storage->super);
  cr_assert(parse_config(snippet, LL_CONTEXT_SERVER_PROTO, &storage->super, (gpointer *) &result),
            "cannot parse transport %s", snippet);
  log_proto_server_options_init(&storage->super, configuration);
  return result;
}

static LogProtoServerFactory *
_parse_lumberjack_transport(const gchar *snippet)
{
  if (options_initialized)
    log_proto_server_options_destroy(&options_storage.super);
  options_initialized = TRUE;
  return _parse_lumberjack_transport_into(&options_storage, snippet);
}

static LumberjackProtoServerOptions *
_options(void)
{
  return (LumberjackProtoServerOptions *) &options_storage;
}

Test(lumberjack, grammar_defaults)
{
  LogProtoServerFactory *factory = _parse_lumberjack_transport("lumberjack()");

  cr_assert_not_null(factory);
  cr_assert_eq(factory->default_inet_port, LUMBERJACK_DEFAULT_PORT);
  cr_assert(!log_proto_server_factory_is_proto_stateful(factory));
  cr_assert_eq(_options()->lumberjack.max_window_size, LUMBERJACK_DEFAULT_MAX_WINDOW_SIZE);
  cr_assert_eq(_options()->lumberjack.keepalive_interval, LUMBERJACK_DEFAULT_KEEPALIVE_INTERVAL);
  cr_assert_eq(_options()->lumberjack.window_timeout, LUMBERJACK_DEFAULT_WINDOW_TIMEOUT);
}

Test(lumberjack, grammar_empty_option_block)
{
  cr_assert_not_null(_parse_lumberjack_transport("lumberjack()"));
  cr_assert_eq(_options()->lumberjack.max_window_size, LUMBERJACK_DEFAULT_MAX_WINDOW_SIZE);
}

Test(lumberjack, grammar_options)
{
  cr_assert_not_null(_parse_lumberjack_transport("lumberjack(max-window-size(2048) keepalive-interval(3) "
                                                 "window-timeout(0))"));
  cr_assert_eq(_options()->lumberjack.max_window_size, 2048);
  cr_assert_eq(_options()->lumberjack.keepalive_interval, 3);
  cr_assert_eq(_options()->lumberjack.window_timeout, 0);
}

Test(lumberjack, grammar_zero_disables_the_window_size_limit)
{
  cr_assert_not_null(_parse_lumberjack_transport("lumberjack(max_window_size(0))"));
  cr_assert_eq(_options()->lumberjack.max_window_size, 0);
}

Test(lumberjack, grammar_installs_the_consecutive_ack_tracker)
{
  _parse_lumberjack_transport("lumberjack()");
  cr_assert_not_null(options_storage.super.ack_tracker_factory);
  cr_assert_eq(ack_tracker_factory_get_type(options_storage.super.ack_tracker_factory), ACK_CONSECUTIVE);
}

/* @tail is the text following the plugin name, up to and including the ')' of
 * transport(), which the plugin grammar must leave behind for its caller */
static LogProtoServerFactory *
_parse_lumberjack_transport_tail(const gchar *tail)
{
  CfgLexer *old_lexer = configuration->lexer;
  CFG_LTYPE yylloc;
  CFG_STYPE yylval;

  memset(&yylloc, 0, sizeof(yylloc));
  yylloc.first_line = yylloc.last_line = 1;
  yylloc.first_column = yylloc.last_column = 1;

  if (options_initialized)
    log_proto_server_options_destroy(&options_storage.super);
  memset(&options_storage, 0, sizeof(options_storage));
  log_proto_server_options_defaults(&options_storage.super);

  Plugin *plugin = cfg_find_plugin(configuration, LL_CONTEXT_SERVER_PROTO, "lumberjack");
  cr_assert_not_null(plugin, "the lumberjack server-proto plugin is not registered");
  cr_assert_not_null(plugin->parser, "the lumberjack plugin must have a grammar of its own");

  CfgLexer *lexer = cfg_lexer_new_buffer(configuration, tail, strlen(tail));
  cr_assert_not_null(lexer);
  configuration->lexer = lexer;
  cfg_lexer_push_context(lexer, main_parser.context, main_parser.keywords, main_parser.name);
  gpointer result = cfg_parse_plugin(configuration, plugin, &yylloc, &options_storage.super);
  cfg_lexer_pop_context(lexer);

  cr_assert_not_null(result, "the lumberjack grammar did not return a LogProtoServerFactory");

  memset(&yylval, 0, sizeof(yylval));
  cr_assert_eq(cfg_lexer_lex(lexer, &yylval, &yylloc), ')',
               "the lumberjack grammar consumed the closing paren of transport()");
  cfg_lexer_free_token(&yylval);

  configuration->lexer = old_lexer;
  cfg_lexer_free(lexer);

  log_proto_server_options_init(&options_storage.super, configuration);
  options_initialized = TRUE;

  return (LogProtoServerFactory *) result;
}

Test(lumberjack, grammar_leaves_the_paren_of_transport_alone)
{
  LogProtoServerFactory *factory = _parse_lumberjack_transport_tail("(max-window-size(5)))");

  cr_assert_eq(factory->default_inet_port, LUMBERJACK_DEFAULT_PORT);
  cr_assert_eq(_options()->lumberjack.max_window_size, 5);
}

Test(lumberjack, grammar_bare_name_leaves_the_paren_of_transport_alone)
{
  _parse_lumberjack_transport_tail(")");
  cr_assert_eq(_options()->lumberjack.max_window_size, LUMBERJACK_DEFAULT_MAX_WINDOW_SIZE);
}

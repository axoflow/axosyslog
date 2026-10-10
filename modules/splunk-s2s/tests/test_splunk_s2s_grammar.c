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

#include "splunk-s2s-options.h"
#include "splunk-s2s-protocol.h"
#include "apphook.h"
#include "cfg.h"
#include "driver.h"
#include "plugin.h"

#include <string.h>

static LogProtoServerOptionsStorage options_storage;
static gboolean options_initialized;
/* the mock transport reads the stream in place, so it outlives the proto */
static GString *proto_stream;

static void
setup(void)
{
  app_startup();
  init_proto_tests();
  cr_assert(cfg_load_module(configuration, "splunk-s2s"), "cannot load the splunk-s2s module");
}

static void
teardown(void)
{
  if (options_initialized)
    {
      log_proto_server_options_destroy(&options_storage.super);
      options_initialized = FALSE;
    }
  if (proto_stream)
    {
      g_string_free(proto_stream, TRUE);
      proto_stream = NULL;
    }
  deinit_proto_tests();
  app_shutdown();
}

TestSuite(splunk_s2s_grammar, .init = setup, .fini = teardown);

/* parse the contents of transport() the way a source driver does; the
 * snippet needs an option block, even an empty one, as parse_config() splits
 * the plugin name off at the first parenthesis */
static LogProtoServerFactory *
_parse_transport(const gchar *config_snippet)
{
  gpointer result = NULL;

  if (options_initialized)
    log_proto_server_options_destroy(&options_storage.super);

  memset(&options_storage, 0, sizeof(options_storage));
  log_proto_server_options_defaults(&options_storage.super);
  options_initialized = TRUE;

  if (!parse_config(config_snippet, LL_CONTEXT_SERVER_PROTO, &options_storage.super, &result))
    return NULL;

  log_proto_server_options_init(&options_storage.super, configuration);
  return (LogProtoServerFactory *) result;
}

static SplunkS2SProtoServerOptions *
_get_options(void)
{
  return (SplunkS2SProtoServerOptions *) &options_storage.super;
}

static void
_assert_transport_is_rejected(const gchar *config_snippet)
{
  start_grabbing_messages();
  LogProtoServerFactory *factory = _parse_transport(config_snippet);
  stop_grabbing_messages();

  cr_assert_null(factory, "transport(%s) was accepted", config_snippet);
}

Test(splunk_s2s_grammar, an_empty_option_block_leaves_lines_unmerged)
{
  LogProtoServerFactory *factory = _parse_transport("splunk-s2s()");

  cr_assert_not_null(factory, "the splunk-s2s grammar did not return a LogProtoServerFactory");
  cr_assert_eq(factory->default_inet_port, 9997);
  cr_assert_eq(_get_options()->multi_line_options.mode, MLM_NONE);
  cr_assert_eq(_get_options()->multi_line_timeout, 0);
}

Test(splunk_s2s_grammar, the_option_block_sets_the_multi_line_options)
{
  LogProtoServerFactory *factory = _parse_transport("splunk-s2s("
                                                    "  multi-line-mode(prefix-garbage)"
                                                    "  multi-line-prefix('^[0-9]{4}-[0-9]{2}-[0-9]{2} ')"
                                                    "  multi-line-garbage('^--END--$')"
                                                    "  multi-line-timeout(5)"
                                                    ")");

  cr_assert_not_null(factory);
  cr_assert_eq(_get_options()->multi_line_options.mode, MLM_REGEXP_PREFIX_GARBAGE);
  cr_assert_not_null(_get_options()->multi_line_options.regexp.prefix);
  cr_assert_not_null(_get_options()->multi_line_options.regexp.garbage);
  cr_assert_eq(_get_options()->multi_line_timeout, 5000);
}

Test(splunk_s2s_grammar, underscored_spellings_are_accepted)
{
  cr_assert_not_null(_parse_transport("splunk_s2s(multi_line_mode(indented) multi_line_timeout(2))"));
  cr_assert_eq(_get_options()->multi_line_options.mode, MLM_INDENTED);
  cr_assert_eq(_get_options()->multi_line_timeout, 2000);
}

Test(splunk_s2s_grammar, an_invalid_multi_line_mode_is_rejected)
{
  _assert_transport_is_rejected("splunk-s2s(multi-line-mode(sideways))");
}

Test(splunk_s2s_grammar, a_prefix_binds_the_regexp_mode)
{
  cr_assert_not_null(_parse_transport("splunk-s2s(multi-line-prefix('^[0-9]'))"));
  cr_assert_eq(_get_options()->multi_line_options.mode, MLM_REGEXP_PREFIX_GARBAGE);

  cr_assert_not_null(_parse_transport("splunk-s2s(multi-line-prefix('^[0-9]') multi-line-mode(prefix-suffix))"));
  cr_assert_eq(_get_options()->multi_line_options.mode, MLM_REGEXP_PREFIX_SUFFIX);

  _assert_transport_is_rejected("splunk-s2s(multi-line-mode(indented) multi-line-prefix('^[0-9]'))");
  _assert_transport_is_rejected("splunk-s2s(multi-line-prefix('^[0-9]') multi-line-mode(timestamp))");
}

Test(splunk_s2s_grammar, a_timeout_without_a_mode_is_rejected)
{
  _assert_transport_is_rejected("splunk-s2s(multi-line-timeout(5))");
}

Test(splunk_s2s_grammar, an_unknown_option_is_rejected)
{
  _assert_transport_is_rejected("splunk-s2s(ack-timeout(5))");
}

/****************************************************************************
 * The factory the grammar hands out
 ****************************************************************************/

#define CHUNK_FLAGS 0x3f

static LogProtoServer *
_construct_proto(LogProtoServerFactory *factory, const gchar *chunk)
{
  if (proto_stream)
    g_string_free(proto_stream, TRUE);
  proto_stream = g_string_new(NULL);

  splunk_s2s_format_header1(proto_stream, "forwarder", "8089");
  splunk_s2s_format_v3_signature_frame(proto_stream, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  splunk_s2s_format_v3_forwarder_info_frame(proto_stream, "ForwarderInfo", "test-guid", 1700000000);
  splunk_s2s_format_open_channel(proto_stream, 1, "/var/log/app.log", "forwarder", "java_app");
  splunk_s2s_format_event(proto_stream, 1, CHUNK_FLAGS, 1700000000, 2, NULL, 0, chunk, strlen(chunk));

  LogTransport *transport = log_transport_mock_stream_new(proto_stream->str, proto_stream->len, LTM_EOF);

  return log_proto_server_factory_construct(factory, transport, &options_storage.super, NULL);
}

static void
_assert_fetched_messages(LogProtoServer *proto, const gchar **expected, gsize n_expected)
{
  Bookmark bookmark;
  LogTransportAuxData aux;
  gboolean may_read = TRUE;
  const guchar *msg = NULL;
  gsize msg_len = 0;
  LogProtoStatus status;
  gsize i = 0;

  log_transport_aux_data_init(&aux);
  while (TRUE)
    {
      log_transport_aux_data_reinit(&aux);
      status = log_proto_server_fetch(proto, &msg, &msg_len, &may_read, &aux, &bookmark);
      if (status != LPS_SUCCESS)
        break;
      if (!msg)
        continue;

      cr_assert_lt(i, n_expected, "unexpected message: %.*s", (gint) msg_len, msg);
      cr_assert_eq(msg_len, strlen(expected[i]), "message mismatch, actual: '%.*s' expected: '%s'",
                   (gint) msg_len, msg, expected[i]);
      cr_assert_arr_eq(msg, expected[i], msg_len);
      i++;
    }
  log_transport_aux_data_destroy(&aux);

  cr_assert_eq(status, LPS_EOF);
  cr_assert_eq(i, n_expected, "fewer messages than expected: %d", (gint) i);
}

Test(splunk_s2s_grammar, the_factory_builds_a_proto_that_merges_lines_as_configured)
{
  LogProtoServerFactory *factory = _parse_transport("splunk-s2s(multi-line-mode(prefix-garbage) "
                                                    "multi-line-prefix('^[0-9]{4}-'))");
  cr_assert_not_null(factory);

  LogProtoServer *proto = _construct_proto(factory, "2026-10-09 first\n\tat A\n2026-10-09 second\n");
  const gchar *expected[] = { "ForwarderInfo", "2026-10-09 first\n\tat A", "2026-10-09 second" };
  _assert_fetched_messages(proto, expected, G_N_ELEMENTS(expected));
  log_proto_server_free(proto);
}

Test(splunk_s2s_grammar, the_factory_builds_a_proto_that_leaves_lines_alone_by_default)
{
  LogProtoServerFactory *factory = _parse_transport("splunk-s2s()");
  cr_assert_not_null(factory);

  LogProtoServer *proto = _construct_proto(factory, "2026-10-09 first\n\tat A\n2026-10-09 second\n");
  const gchar *expected[] = { "ForwarderInfo", "2026-10-09 first", "\tat A", "2026-10-09 second" };
  _assert_fetched_messages(proto, expected, G_N_ELEMENTS(expected));
  log_proto_server_free(proto);
}

Test(splunk_s2s_grammar, the_timestamp_mode_breaks_events_on_leading_timestamps)
{
  LogProtoServerFactory *factory = _parse_transport("splunk-s2s(multi-line-mode(timestamp) "
                                                    "multi-line-timestamp-pairs('[]()'))");
  cr_assert_not_null(factory);
  cr_assert_eq(_get_options()->multi_line_options.mode, MLM_TIMESTAMP);
  cr_assert_str_eq(_get_options()->multi_line_options.timestamp.pairs, "[]()");

  LogProtoServer *proto = _construct_proto(factory, "(2026-10-09 15:31:50) first\n\tat A\n"
                                                    "[Oct  9 15:31:51] second\n2026-10-09T15:31:52Z third\n");
  const gchar *expected[] =
  {
    "ForwarderInfo", "(2026-10-09 15:31:50) first\n\tat A", "[Oct  9 15:31:51] second", "2026-10-09T15:31:52Z third"
  };
  _assert_fetched_messages(proto, expected, G_N_ELEMENTS(expected));
  log_proto_server_free(proto);
}

Test(splunk_s2s_grammar, timestamp_pairs_bind_the_timestamp_mode)
{
  cr_assert_not_null(_parse_transport("splunk-s2s(multi-line-timestamp-pairs('[]()'))"));
  cr_assert_eq(_get_options()->multi_line_options.mode, MLM_TIMESTAMP);
  cr_assert_str_eq(_get_options()->multi_line_options.timestamp.pairs, "[]()");

  /* whichever order the mode of another family comes in */
  _assert_transport_is_rejected("splunk-s2s(multi-line-mode(indented) multi-line-timestamp-pairs('[]'))");
  _assert_transport_is_rejected("splunk-s2s(multi-line-timestamp-pairs('[]') multi-line-mode(prefix-garbage))");
  _assert_transport_is_rejected("splunk-s2s(multi-line-mode(prefix-garbage) multi-line-timestamp-pairs('[]'))");
}

Test(splunk_s2s_grammar, timestamp_pairs_need_whole_pairs)
{
  _assert_transport_is_rejected("splunk-s2s(multi-line-mode(timestamp) multi-line-timestamp-pairs('[]('))");
}

/****************************************************************************
 * In a network() source
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

Test(splunk_s2s_grammar, a_network_source_accepts_the_bare_and_the_quoted_transport)
{
  _assert_network_source_parses("network(port(9997) transport(splunk-s2s))");
  _assert_network_source_parses("network(port(9997) transport(\"splunk-s2s\"))");
}

Test(splunk_s2s_grammar, a_network_source_accepts_the_option_block)
{
  _assert_network_source_parses("network(port(9997) "
                                "transport(splunk-s2s(multi-line-mode(prefix-garbage) "
                                "multi-line-prefix('^[0-9]{4}-') multi-line-timeout(10))) "
                                "flags(no-parse) log-msg-size(4194304))");
}

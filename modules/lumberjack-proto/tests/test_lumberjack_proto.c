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
#include "logmsg/logmsg.h"
#include "transport/transport-stack.h"

#include <errno.h>
#include <string.h>
#include <zlib.h>

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

/****************************************************************************
 * A fake TLS transport, so a test can tell a connection with tls() from one without.
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
 * Wire format helpers (specification 4, 5, 6)
 ****************************************************************************/

static void
_put_u32(GString *s, guint32 v)
{
  guchar buf[4] = { v >> 24, v >> 16, v >> 8, v };

  g_string_append_len(s, (const gchar *) buf, 4);
}

static guint32
_read_u32_from(const guchar *p)
{
  return ((guint32) p[0] << 24) | ((guint32) p[1] << 16) | ((guint32) p[2] << 8) | (guint32) p[3];
}

static void
_put_window(GString *s, gchar version, guint32 size)
{
  g_string_append_c(s, version);
  g_string_append_c(s, 'W');
  _put_u32(s, size);
}

static void
_put_json_frame_len(GString *s, guint32 seq, const gchar *payload, gsize len)
{
  g_string_append_c(s, '2');
  g_string_append_c(s, 'J');
  _put_u32(s, seq);
  _put_u32(s, len);
  g_string_append_len(s, payload, len);
}

static void
_put_json_frame(GString *s, guint32 seq, const gchar *payload)
{
  _put_json_frame_len(s, seq, payload, strlen(payload));
}

/* a version 1 data frame of NUL terminated key, value pairs, ending in NULL */
static void
_put_data_frame(GString *s, guint32 seq, guint32 count, ...)
{
  va_list va;

  g_string_append_c(s, '1');
  g_string_append_c(s, 'D');
  _put_u32(s, seq);
  _put_u32(s, count);

  va_start(va, count);
  for (guint32 i = 0; i < count; i++)
    {
      const gchar *key = va_arg(va, const gchar *);
      const gchar *value = va_arg(va, const gchar *);

      _put_u32(s, strlen(key));
      g_string_append(s, key);
      _put_u32(s, strlen(value));
      g_string_append(s, value);
    }
  va_end(va);
}

/* a `C` frame of @version carrying @frames deflated at @level (specification 9.1) */
/* a version 1 frame of a single pair whose value may be binary */
static void
_put_data_frame_1(GString *s, guint32 seq, const gchar *key, const gchar *value, gsize value_len)
{
  g_string_append_c(s, '1');
  g_string_append_c(s, 'D');
  _put_u32(s, seq);
  _put_u32(s, 1);
  _put_u32(s, strlen(key));
  g_string_append(s, key);
  _put_u32(s, value_len);
  g_string_append_len(s, value, value_len);
}

static void
_put_compressed(GString *s, gchar version, const GString *frames, gint level)
{
  uLongf compressed_len = compressBound(frames->len);
  guchar *compressed = g_malloc(compressed_len);

  cr_assert_eq(compress2(compressed, &compressed_len, (const Bytef *) frames->str, frames->len, level), Z_OK);
  g_string_append_c(s, version);
  g_string_append_c(s, 'C');
  _put_u32(s, compressed_len);
  g_string_append_len(s, (const gchar *) compressed, compressed_len);
  g_free(compressed);
}

static GString *
_json_frames(guint32 first, guint32 count, const gchar *prefix)
{
  GString *frames = g_string_new("");

  for (guint32 i = first; i < first + count; i++)
    {
      gchar *payload = g_strdup_printf("{\"message\":\"%s-%u\"}", prefix, i);

      _put_json_frame(frames, i, payload);
      g_free(payload);
    }
  return frames;
}

/* the way the canonical sender compresses: one `C` frame per window covering
 * every data frame (9.2) */
static GString *
_compressed_window_of_json(guint32 size, const gchar *prefix, gint level)
{
  GString *frames = _json_frames(1, size, prefix);
  GString *s = g_string_new("");

  _put_window(s, '2', size);
  _put_compressed(s, '2', frames, level);
  g_string_free(frames, TRUE);
  return s;
}

static GString *
_ack(gchar version, guint32 seq)
{
  GString *s = g_string_new("");

  g_string_append_c(s, version);
  g_string_append_c(s, 'A');
  _put_u32(s, seq);
  return s;
}

/****************************************************************************
 * A transport that tells the pump whether the mock underneath ran dry
 ****************************************************************************/

typedef struct _ObservedTransport
{
  LogTransport super;
  LogTransport *mock;
  /* the last read hit the end of the injected input */
  gboolean dry;
} ObservedTransport;

static gssize
_observed_read(LogTransport *s, gpointer buf, gsize count, LogTransportAuxData *aux)
{
  ObservedTransport *self = (ObservedTransport *) s;
  gssize rc = log_transport_read(self->mock, buf, count, aux);

  if (rc < 0 && errno == EAGAIN)
    self->dry = TRUE;
  return rc;
}

static gssize
_observed_write(LogTransport *s, const gpointer buf, gsize count)
{
  ObservedTransport *self = (ObservedTransport *) s;

  return log_transport_write(self->mock, buf, count);
}

static void
_observed_free(LogTransport *s)
{
  ObservedTransport *self = (ObservedTransport *) s;

  log_transport_free(self->mock);
  log_transport_free_method(s);
}

static LogTransport *
_observed_transport_new(LogTransport *mock)
{
  ObservedTransport *self = g_new0(ObservedTransport, 1);

  log_transport_init_instance(&self->super, "observed", 0);
  self->super.read = _observed_read;
  self->super.write = _observed_write;
  self->super.free_fn = _observed_free;
  self->mock = mock;
  return &self->super;
}

/****************************************************************************
 * A connection, driven the way LogReader drives it
 ****************************************************************************/

typedef enum
{
  /* poll_prepare() asked for I/O and fetch() had nothing more to do */
  LUMBERJACK_PUMP_IDLE,
  LUMBERJACK_PUMP_WRITTEN,
  LUMBERJACK_PUMP_MESSAGES,
  LUMBERJACK_PUMP_EOF,
  LUMBERJACK_PUMP_ERROR,
} LumberjackPumpResult;

typedef struct _LumberjackTestConnection
{
  /* the mock the proto reads from and writes to, wrapped in the observed one */
  LogTransportMock *mock;
  ObservedTransport *transport;
  LogProtoServer *proto;
  GString *written;
  GPtrArray *messages;
  GPtrArray *bookmarks;
  /* the mock transport reads the injected input by reference, so it is kept
   * alive as long as the connection */
  GPtrArray *inputs;
  /* fetch() is given no Bookmark, the way the ack tracker refuses one when the
   * flow-control window is full */
  gboolean window_full;
  /* the last poll_prepare() results */
  GIOCondition cond;
  gint timeout;
  LogProtoPrepareAction action;
} LumberjackTestConnection;

static void
_free_bookmark(gpointer data)
{
  Bookmark *bookmark = (Bookmark *) data;

  bookmark_destroy(bookmark);
  g_free(bookmark);
}

static void
_free_input(gpointer data)
{
  g_string_free((GString *) data, TRUE);
}

/* @input, when given, is what the transport starts out with; NULL for an
 * endless connection that only reads what _inject() gives it.  @snippet is the
 * transport(...) text the connection is configured with. */
static void
_connection_init_full(LumberjackTestConnection *self, const gchar *snippet, GString *input, gboolean stream,
                      gboolean with_tls)
{
  LogProtoServerFactory *factory = _parse_lumberjack_transport(snippet);
  LogTransport *mock;

  memset(self, 0, sizeof(*self));
  self->inputs = g_ptr_array_new_with_free_func(_free_input);
  if (input)
    {
      g_ptr_array_add(self->inputs, input);
      if (stream)
        mock = log_transport_mock_endless_stream_new(input->str, (gssize) input->len, LTM_EOF);
      else
        mock = log_transport_mock_records_new(input->str, (gssize) input->len, LTM_EOF);
    }
  else
    mock = log_transport_mock_endless_records_new(LTM_EOF);
  self->mock = (LogTransportMock *) mock;
  self->transport = (ObservedTransport *) _observed_transport_new(mock);

  self->proto = log_proto_server_factory_construct(factory, &self->transport->super, &options_storage.super, NULL);
  cr_assert_not_null(self->proto);

  if (with_tls)
    log_transport_stack_add_factory(&self->proto->transport_stack, _fake_tls_transport_factory_new());
  cr_assert(log_proto_server_validate_options(self->proto));

  self->written = g_string_new("");
  self->messages = g_ptr_array_new_with_free_func((GDestroyNotify) log_msg_unref);
  self->bookmarks = g_ptr_array_new_with_free_func(_free_bookmark);
}

/* an endless connection: reads past the injected input return EAGAIN */
static void
_connection_init(LumberjackTestConnection *self)
{
  _connection_init_full(self, "lumberjack()", NULL, FALSE, FALSE);
}

static void
_connection_init_with(LumberjackTestConnection *self, const gchar *snippet)
{
  _connection_init_full(self, snippet, NULL, FALSE, FALSE);
}

/* an endless connection whose transport returns a single octet per read */
static void
_connection_init_stream(LumberjackTestConnection *self, GString *input)
{
  _connection_init_full(self, "lumberjack()", input, TRUE, FALSE);
}

static void
_connection_deinit(LumberjackTestConnection *self)
{
  log_proto_server_free(self->proto);
  g_ptr_array_free(self->bookmarks, TRUE);
  g_ptr_array_free(self->messages, TRUE);
  g_ptr_array_free(self->inputs, TRUE);
  g_string_free(self->written, TRUE);
}

static void
_inject(LumberjackTestConnection *self, GString *input)
{
  log_transport_mock_inject_data(self->mock, input->str, input->len);
  g_ptr_array_add(self->inputs, input);
  self->transport->dry = FALSE;
}

static void
_collect_written(LumberjackTestConnection *self)
{
  gchar buffer[4096];
  gssize len;

  while ((len = log_transport_mock_read_from_write_buffer(self->mock, buffer, sizeof(buffer))) > 0)
    g_string_append_len(self->written, buffer, len);
}

/* Run poll_prepare() and fetch() the way LogReader does until the proto is
 * idle, ends, errors out, or @wanted_written octets or @wanted_messages
 * messages have arrived.  Idle means poll_prepare() asked to poll for input and
 * the transport has none, or the flow-control window is full and the proto did
 * not ask for writability, which is when LogReader suspends. */
static LumberjackPumpResult
_pump_until(LumberjackTestConnection *self, gsize wanted_written, guint wanted_messages)
{
  for (gint round = 0; round < 1000000; round++)
    {
      LogMessage *msg = NULL;
      LogTransportAuxData aux;
      LogProtoStatus status;

      if (wanted_written && self->written->len >= wanted_written)
        return LUMBERJACK_PUMP_WRITTEN;
      if (wanted_messages && self->messages->len >= wanted_messages)
        return LUMBERJACK_PUMP_MESSAGES;

      self->cond = 0;
      self->timeout = -1;
      self->action = log_proto_server_poll_prepare(self->proto, &self->cond, &self->timeout);
      cr_assert_neq(self->action, LPPA_SUSPEND, "the lumberjack proto never suspends itself");

      gboolean wants_write = self->action == LPPA_POLL_IO && self->cond == G_IO_OUT;

      if (self->window_full && !wants_write)
        return LUMBERJACK_PUMP_IDLE;
      if (self->action == LPPA_POLL_IO && !wants_write && self->transport->dry)
        return LUMBERJACK_PUMP_IDLE;

      Bookmark *bookmark = self->window_full ? NULL : g_new0(Bookmark, 1);

      log_transport_aux_data_init(&aux);
      status = log_proto_server_fetch_structured(self->proto, &msg, &aux, bookmark);
      log_transport_aux_data_destroy(&aux);

      if (msg)
        {
          cr_assert_not_null(bookmark, "a message was delivered with no Bookmark to carry it");
          g_ptr_array_add(self->messages, msg);
          g_ptr_array_add(self->bookmarks, bookmark);
        }
      else if (bookmark)
        _free_bookmark(bookmark);

      _collect_written(self);

      if (status == LPS_EOF)
        return LUMBERJACK_PUMP_EOF;
      if (status == LPS_ERROR)
        return LUMBERJACK_PUMP_ERROR;
    }

  cr_assert_fail("the lumberjack proto neither went idle nor reached a terminal status");
  return LUMBERJACK_PUMP_ERROR;
}

static LumberjackPumpResult
_pump(LumberjackTestConnection *self)
{
  return _pump_until(self, 0, 0);
}

static void
_assert_written(LumberjackTestConnection *self, GString *expected)
{
  _pump_until(self, expected->len, 0);
  cr_assert_eq(self->written->len, expected->len, "%u octets were written, %u expected",
               (guint) self->written->len, (guint) expected->len);
  cr_assert_arr_eq(self->written->str, expected->str, expected->len);
  g_string_free(expected, TRUE);
}

static void
_assert_nothing_written(LumberjackTestConnection *self)
{
  _pump(self);
  cr_assert_eq(self->written->len, 0, "%u octets were written, none expected", (guint) self->written->len);
}

static void
_assert_message(LumberjackTestConnection *self, guint index, const gchar *payload, const gchar *version)
{
  cr_assert_gt(self->messages->len, index, "the proto delivered %u messages, not %u",
               self->messages->len, index + 1);

  LogMessage *msg = (LogMessage *) g_ptr_array_index(self->messages, index);
  gssize len;
  const gchar *value = log_msg_get_value(msg, LM_V_MESSAGE, &len);

  cr_assert_eq((gsize) len, strlen(payload), "message %u is %u octets long, not %u", index, (guint) len,
               (guint) strlen(payload));
  cr_assert_arr_eq(value, payload, len);

  value = log_msg_get_value_by_name(msg, ".lumberjack.version", &len);
  cr_assert_eq(len, 1);
  cr_assert_eq(value[0], version[0], "message %u carries version %c, not %c", index, value[0], version[0]);
}

/* as the consecutive ack tracker does on the destination thread, for the last
 * message of a newly durable prefix */
static void
_report_durable(LumberjackTestConnection *self, guint index)
{
  cr_assert_gt(self->bookmarks->len, index);
  bookmark_save((Bookmark *) g_ptr_array_index(self->bookmarks, index));
}

static GString *
_window_of_json(guint32 size, const gchar *prefix)
{
  GString *s = g_string_new("");

  _put_window(s, '2', size);
  for (guint32 i = 1; i <= size; i++)
    {
      gchar *payload = g_strdup_printf("{\"message\":\"%s-%u\"}", prefix, i);

      _put_json_frame(s, i, payload);
      g_free(payload);
    }
  return s;
}

/****************************************************************************
 * Version 2 windows, delivery and acknowledgement (specification 6.2, 7, 8)
 ****************************************************************************/

Test(lumberjack, frames_become_messages_as_they_arrive)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _inject(&conn, _window_of_json(3, "m"));

  cr_assert_eq(_pump_until(&conn, 0, 3), LUMBERJACK_PUMP_MESSAGES);
  _assert_message(&conn, 0, "{\"message\":\"m-1\"}", "2");
  _assert_message(&conn, 1, "{\"message\":\"m-2\"}", "2");
  _assert_message(&conn, 2, "{\"message\":\"m-3\"}", "2");

  /* delivered is not durable: no ACK yet */
  _assert_nothing_written(&conn);
  _connection_deinit(&conn);
}

Test(lumberjack, window_is_acknowledged_once_its_last_frame_is_durable)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _inject(&conn, _window_of_json(3, "m"));
  _pump_until(&conn, 0, 3);

  _report_durable(&conn, 1);
  _assert_nothing_written(&conn);

  _report_durable(&conn, 2);
  _assert_written(&conn, _ack('2', 3));
  _connection_deinit(&conn);
}

Test(lumberjack, ack_is_written_even_when_the_flow_control_window_is_full)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _inject(&conn, _window_of_json(2, "m"));
  _pump_until(&conn, 0, 2);
  /* the next window is buffered but the window is exhausted */
  _inject(&conn, _window_of_json(1, "n"));
  conn.window_full = TRUE;

  _report_durable(&conn, 1);
  GIOCondition cond = 0;
  gint timeout = -1;
  cr_assert_eq(log_proto_server_poll_prepare(conn.proto, &cond, &timeout), LPPA_POLL_IO);
  cr_assert_eq(cond, G_IO_OUT, "an ACK that is due polls for writability only");

  _assert_written(&conn, _ack('2', 2));
  cr_assert_eq(conn.messages->len, 2, "no message may be delivered while the window is exhausted");

  conn.window_full = FALSE;
  _pump_until(&conn, 0, 3);
  _assert_message(&conn, 2, "{\"message\":\"n-1\"}", "2");
  _connection_deinit(&conn);
}

Test(lumberjack, pipelined_windows_are_acknowledged_in_order)
{
  LumberjackTestConnection conn;
  GString *expected = _ack('2', 2);
  GString *second = _ack('2', 3);

  _connection_init(&conn);
  _inject(&conn, _window_of_json(2, "a"));
  _inject(&conn, _window_of_json(3, "b"));
  _pump_until(&conn, 0, 5);
  _assert_message(&conn, 4, "{\"message\":\"b-3\"}", "2");

  /* the consecutive ack tracker only reports the end of the durable prefix */
  _report_durable(&conn, 4);

  g_string_append_len(expected, second->str, second->len);
  g_string_free(second, TRUE);
  _assert_written(&conn, expected);
  _connection_deinit(&conn);
}

Test(lumberjack, durability_of_the_first_window_acknowledges_the_first_only)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _inject(&conn, _window_of_json(2, "a"));
  _inject(&conn, _window_of_json(3, "b"));
  _pump_until(&conn, 0, 5);

  _report_durable(&conn, 1);
  _assert_written(&conn, _ack('2', 2));

  _report_durable(&conn, 3);
  _pump(&conn);
  cr_assert_eq(conn.written->len, 6, "the second window is not durable yet");
  _connection_deinit(&conn);
}

Test(lumberjack, a_window_of_size_zero_is_a_no_op)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '2', 0);
  _put_window(input, '2', 1);
  _put_json_frame(input, 1, "{}");

  _connection_init(&conn);
  _inject(&conn, input);
  _pump_until(&conn, 0, 1);
  _assert_message(&conn, 0, "{}", "2");
  _report_durable(&conn, 0);
  _assert_written(&conn, _ack('2', 1));
  _connection_deinit(&conn);
}

Test(lumberjack, frames_arriving_one_octet_at_a_time_are_reassembled)
{
  LumberjackTestConnection conn;
  GString *input = _window_of_json(2, "slow");

  /* the stream mock returns a single octet per read */
  _connection_init_stream(&conn, input);

  cr_assert_eq(_pump_until(&conn, 0, 2), LUMBERJACK_PUMP_MESSAGES);
  _assert_message(&conn, 1, "{\"message\":\"slow-2\"}", "2");
  _report_durable(&conn, 1);
  _assert_written(&conn, _ack('2', 2));
  _connection_deinit(&conn);
}

Test(lumberjack, payload_is_delivered_verbatim_including_nuls_and_invalid_utf8)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  const gchar payload[] = "\"\xff\0\x01 not json at all";

  _put_window(input, '2', 1);
  _put_json_frame_len(input, 1, payload, sizeof(payload) - 1);

  _connection_init(&conn);
  _inject(&conn, input);
  _pump_until(&conn, 0, 1);

  LogMessage *msg = (LogMessage *) g_ptr_array_index(conn.messages, 0);
  gssize len;
  const gchar *value = log_msg_get_value(msg, LM_V_MESSAGE, &len);
  cr_assert_eq((gsize) len, sizeof(payload) - 1);
  cr_assert_arr_eq(value, payload, len);
  _connection_deinit(&conn);
}

Test(lumberjack, eof_in_the_middle_of_a_window_acknowledges_nothing)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '2', 2);
  _put_json_frame(input, 1, "{\"a\":1}");

  /* the records mock returns EOF once the input is consumed */
  _connection_init_full(&conn, "lumberjack()", input, FALSE, FALSE);

  cr_assert_eq(_pump(&conn), LUMBERJACK_PUMP_EOF);
  cr_assert_eq(conn.messages->len, 1);
  _report_durable(&conn, 0);
  cr_assert_eq(conn.written->len, 0);
  _connection_deinit(&conn);
}

/****************************************************************************
 * Version 1 (specification 5.2)
 ****************************************************************************/

Test(lumberjack, version1_data_frame_becomes_a_flat_json_object)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '1', 1);
  _put_data_frame(input, 1, 3, "line", "hello world", "host", "web-1", "offset", "42");

  _connection_init(&conn);
  _inject(&conn, input);
  _pump_until(&conn, 0, 1);
  _assert_message(&conn, 0, "{\"line\":\"hello world\",\"host\":\"web-1\",\"offset\":\"42\"}", "1");

  _report_durable(&conn, 0);
  _assert_written(&conn, _ack('1', 1));
  _connection_deinit(&conn);
}

Test(lumberjack, version1_duplicate_keys_are_kept_and_strings_are_escaped)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '1', 1);
  /* a quote, a backslash, a newline, a control character, valid UTF-8 and an
   * invalid UTF-8 octet */
  _put_data_frame(input, 1, 2, "k", "say \"hi\"\\\n", "k", "\x01\xc3\xa9\xff");

  _connection_init(&conn);
  _inject(&conn, input);
  _pump_until(&conn, 0, 1);
  _assert_message(&conn, 0, "{\"k\":\"say \\\"hi\\\"\\\\\\n\",\"k\":\"\\u0001\xc3\xa9\\\\xff\"}", "1");
  _connection_deinit(&conn);
}

/* the input buffer starts at init_buffer_size, 64 octets at the least, and
 * has to grow to hold a field that is longer than that */
Test(lumberjack, version1_field_larger_than_the_initial_buffer_is_reassembled)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  GString *expected = g_string_new("{\"k\":\"");
  gchar value[200];

  memset(value, 'x', sizeof(value));
  g_string_append_len(expected, value, sizeof(value));
  g_string_append(expected, "\"}");
  _put_window(input, '1', 1);
  _put_data_frame_1(input, 1, "k", value, sizeof(value));

  /* reads fill whatever room the buffer has */
  _connection_init_stream(&conn, input);
  options_storage.super.init_buffer_size = 16;
  options_storage.super.max_msg_size = 1000;
  cr_assert_eq(_pump_until(&conn, 0, 1), LUMBERJACK_PUMP_MESSAGES);
  _assert_message(&conn, 0, expected->str, "1");
  g_string_free(expected, TRUE);
  _connection_deinit(&conn);
}

Test(lumberjack, json_payload_larger_than_the_initial_buffer_is_reassembled)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  gchar payload[200];

  memset(payload, 'y', sizeof(payload));
  payload[0] = '{';
  payload[sizeof(payload) - 1] = '}';
  _put_window(input, '2', 1);
  _put_json_frame_len(input, 1, payload, sizeof(payload));

  _connection_init_stream(&conn, input);
  options_storage.super.init_buffer_size = 16;
  options_storage.super.max_msg_size = 1000;
  cr_assert_eq(_pump_until(&conn, 0, 1), LUMBERJACK_PUMP_MESSAGES);
  LogMessage *msg = (LogMessage *) g_ptr_array_index(conn.messages, 0);
  gssize len;
  const gchar *value = log_msg_get_value(msg, LM_V_MESSAGE, &len);
  cr_assert_eq((gsize) len, sizeof(payload));
  cr_assert_arr_eq(value, payload, sizeof(payload));
  _connection_deinit(&conn);
}

Test(lumberjack, version1_empty_data_frame_is_an_empty_object)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '1', 2);
  _put_data_frame(input, 1, 0);
  _put_data_frame(input, 2, 1, "", "");

  _connection_init(&conn);
  _inject(&conn, input);
  _pump_until(&conn, 0, 2);
  _assert_message(&conn, 0, "{}", "1");
  _assert_message(&conn, 1, "{\"\":\"\"}", "1");
  _connection_deinit(&conn);
}

Test(lumberjack, version1_data_frame_arriving_one_octet_at_a_time_is_reassembled)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '1', 1);
  _put_data_frame(input, 1, 2, "a", "1", "b", "2");

  _connection_init_stream(&conn, input);
  _pump_until(&conn, 0, 1);
  _assert_message(&conn, 0, "{\"a\":\"1\",\"b\":\"2\"}", "1");
  _connection_deinit(&conn);
}

/****************************************************************************
 * Size limits (specification 16)
 ****************************************************************************/

Test(lumberjack, oversize_json_frame_is_dropped_and_still_counted)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  gchar big[200];

  memset(big, 'x', sizeof(big));
  _put_window(input, '2', 3);
  _put_json_frame(input, 1, "{\"n\":1}");
  _put_json_frame_len(input, 2, big, sizeof(big));
  _put_json_frame(input, 3, "{\"n\":3}");

  _connection_init(&conn);
  options_storage.super.max_msg_size = 100;

  start_grabbing_messages();
  _inject(&conn, input);
  cr_assert_eq(_pump_until(&conn, 0, 2), LUMBERJACK_PUMP_MESSAGES);
  stop_grabbing_messages();
  assert_grabbed_log_contains("Dropping a message larger than log-msg-size()");

  _assert_message(&conn, 0, "{\"n\":1}", "2");
  _assert_message(&conn, 1, "{\"n\":3}", "2");

  /* the ACK covers all three: the dropped frame must not be resent forever */
  _report_durable(&conn, 1);
  _assert_written(&conn, _ack('2', 3));
  _connection_deinit(&conn);
}

Test(lumberjack, window_of_dropped_frames_only_is_acknowledged_at_once)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  gchar big[200];

  memset(big, 'x', sizeof(big));
  _put_window(input, '2', 1);
  _put_json_frame_len(input, 1, big, sizeof(big));

  _connection_init(&conn);
  options_storage.super.max_msg_size = 100;
  _inject(&conn, input);
  _assert_written(&conn, _ack('2', 1));
  cr_assert_eq(conn.messages->len, 0);
  _connection_deinit(&conn);
}

Test(lumberjack, oversize_frame_larger_than_the_buffer_is_skipped_as_it_arrives)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  gchar big[100000];

  memset(big, 'y', sizeof(big));
  _put_window(input, '2', 2);
  _put_json_frame_len(input, 1, big, sizeof(big));
  _put_json_frame(input, 2, "{\"n\":2}");

  /* one octet per read, so the payload can never be buffered whole */
  _connection_init_stream(&conn, input);
  options_storage.super.max_msg_size = 100;

  _pump_until(&conn, 0, 1);
  _assert_message(&conn, 0, "{\"n\":2}", "2");
  _report_durable(&conn, 0);
  _assert_written(&conn, _ack('2', 2));
  _connection_deinit(&conn);
}

Test(lumberjack, payload_of_exactly_log_msg_size_is_accepted)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  gchar payload[100];

  memset(payload, 'z', sizeof(payload));
  _put_window(input, '2', 1);
  _put_json_frame_len(input, 1, payload, sizeof(payload));

  _connection_init(&conn);
  options_storage.super.max_msg_size = 100;
  _inject(&conn, input);
  _pump_until(&conn, 0, 1);
  cr_assert_eq(conn.messages->len, 1);
  _connection_deinit(&conn);
}

Test(lumberjack, oversize_version1_frame_is_dropped_pair_by_pair)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  gchar big[200];

  memset(big, 'v', sizeof(big) - 1);
  big[sizeof(big) - 1] = 0;
  _put_window(input, '1', 2);
  _put_data_frame(input, 1, 3, "a", "1", "b", big, "c", "3");
  _put_data_frame(input, 2, 1, "d", "4");

  _connection_init_stream(&conn, input);
  options_storage.super.max_msg_size = 100;

  _pump_until(&conn, 0, 1);
  _assert_message(&conn, 0, "{\"d\":\"4\"}", "1");
  _report_durable(&conn, 0);
  _assert_written(&conn, _ack('1', 2));
  _connection_deinit(&conn);
}

Test(lumberjack, window_above_max_window_size_closes_the_connection)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '2', 10001);

  _connection_init(&conn);
  start_grabbing_messages();
  _inject(&conn, input);
  cr_assert_eq(_pump(&conn), LUMBERJACK_PUMP_EOF);
  stop_grabbing_messages();
  assert_grabbed_log_contains("window size exceeds max-window-size()");
  cr_assert_eq(conn.written->len, 0);
  _connection_deinit(&conn);
}

Test(lumberjack, max_window_size_zero_is_unlimited)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '2', 1000000);
  _put_json_frame(input, 1, "{}");

  _connection_init_with(&conn, "lumberjack(max-window-size(0))");
  _inject(&conn, input);
  cr_assert_eq(_pump_until(&conn, 0, 1), LUMBERJACK_PUMP_MESSAGES);
  _connection_deinit(&conn);
}

/****************************************************************************
 * Protocol errors (specification 14.1)
 ****************************************************************************/

static void
_assert_protocol_error(GString *input, const gchar *expected_log)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  start_grabbing_messages();
  _inject(&conn, input);
  cr_assert_eq(_pump(&conn), LUMBERJACK_PUMP_EOF, "a protocol error must close the connection");
  stop_grabbing_messages();
  assert_grabbed_log_contains(expected_log);
  cr_assert_eq(conn.written->len, 0, "nothing is acknowledged on a protocol error");
  _connection_deinit(&conn);
}

Test(lumberjack, invalid_version_byte_is_a_protocol_error)
{
  GString *input = g_string_new("");

  g_string_append_len(input, "3W\0\0\0\1", 6);
  _assert_protocol_error(input, "Invalid Lumberjack protocol version byte");
}

Test(lumberjack, tls_client_hello_on_plaintext_is_hinted_at)
{
  GString *input = g_string_new("");

  g_string_append_len(input, "\x16\x03\x01\x02\x00\x01", 6);
  _assert_protocol_error(input, "TLS ClientHello");
}

Test(lumberjack, data_frame_outside_a_window_is_a_protocol_error)
{
  GString *input = g_string_new("");

  _put_json_frame(input, 1, "{}");
  _assert_protocol_error(input, "a window frame was expected");
}

Test(lumberjack, window_frame_inside_a_window_is_a_protocol_error)
{
  GString *input = g_string_new("");

  _put_window(input, '2', 2);
  _put_json_frame(input, 1, "{}");
  _put_window(input, '2', 1);
  _assert_protocol_error(input, "window frame inside a window");
}

/* Sequence numbers are not enforced (specification 8.2, 8.3): frames are
 * counted by position and the window is acknowledged with the last sequence
 * number seen, which is what a sender numbering from a running counter, like
 * logstash-forwarder or ferro-lumberjack, expects. */
Test(lumberjack, out_of_sequence_frames_are_accepted_and_the_last_sequence_is_echoed)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '2', 2);
  _put_json_frame(input, 1, "{\"n\":1}");
  _put_json_frame(input, 3, "{\"n\":3}");

  _connection_init(&conn);
  _inject(&conn, input);
  cr_assert_eq(_pump_until(&conn, 0, 2), LUMBERJACK_PUMP_MESSAGES);
  _assert_message(&conn, 1, "{\"n\":3}", "2");

  _report_durable(&conn, 1);
  _assert_written(&conn, _ack('2', 3));
  _connection_deinit(&conn);
}

Test(lumberjack, running_sequence_counter_across_windows_is_echoed_per_window)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  GString *expected = _ack('2', 2);
  GString *second = _ack('2', 5);

  _put_window(input, '2', 2);
  _put_json_frame(input, 1, "{}");
  _put_json_frame(input, 2, "{}");
  _put_window(input, '2', 3);
  _put_json_frame(input, 3, "{}");
  _put_json_frame(input, 4, "{}");
  _put_json_frame(input, 5, "{}");

  _connection_init(&conn);
  _inject(&conn, input);
  _pump_until(&conn, 0, 5);
  _report_durable(&conn, 4);

  g_string_append_len(expected, second->str, second->len);
  g_string_free(second, TRUE);
  _assert_written(&conn, expected);
  _connection_deinit(&conn);
}

/* A(0) is the keepalive (specification 10.2), so a window whose last frame
 * carries sequence number 0 is acknowledged by count instead. */
Test(lumberjack, last_sequence_of_zero_is_acknowledged_by_count)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '2', 2);
  _put_json_frame(input, 0, "{}");
  _put_json_frame(input, 0, "{}");

  _connection_init(&conn);
  _inject(&conn, input);
  _pump_until(&conn, 0, 2);
  _report_durable(&conn, 1);
  _assert_written(&conn, _ack('2', 2));
  _connection_deinit(&conn);
}

Test(lumberjack, dropped_last_frame_still_sets_the_acknowledged_sequence)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  gchar big[200];

  memset(big, 'x', sizeof(big));
  _put_window(input, '2', 2);
  _put_json_frame(input, 7, "{}");
  _put_json_frame_len(input, 8, big, sizeof(big));

  _connection_init(&conn);
  options_storage.super.max_msg_size = 100;
  _inject(&conn, input);
  _pump_until(&conn, 0, 1);
  _report_durable(&conn, 0);
  _assert_written(&conn, _ack('2', 8));
  _connection_deinit(&conn);
}

Test(lumberjack, frame_version_differing_from_the_window_is_a_protocol_error)
{
  GString *input = g_string_new("");

  _put_window(input, '1', 1);
  _put_json_frame(input, 1, "{}");
  _assert_protocol_error(input, "frame version differs from the version of its window");
}

Test(lumberjack, data_frame_in_a_version2_window_is_a_protocol_error)
{
  GString *input = g_string_new("");

  _put_window(input, '2', 1);
  g_string_append(input, "2D");
  _put_u32(input, 1);
  _put_u32(input, 0);
  _assert_protocol_error(input, "data frame ('D') in a version 2 window");
}

Test(lumberjack, json_frame_in_a_version1_window_is_a_protocol_error)
{
  GString *input = g_string_new("");

  _put_window(input, '1', 1);
  g_string_append(input, "1J");
  _put_u32(input, 1);
  _put_u32(input, 2);
  g_string_append(input, "{}");
  _assert_protocol_error(input, "JSON frame ('J') in a version 1 window");
}

Test(lumberjack, empty_json_payload_is_a_protocol_error)
{
  GString *input = g_string_new("");

  _put_window(input, '2', 1);
  _put_json_frame_len(input, 1, "", 0);
  _assert_protocol_error(input, "JSON frame with an empty payload");
}

Test(lumberjack, unknown_frame_type_is_a_protocol_error)
{
  GString *input = g_string_new("");

  _put_window(input, '2', 1);
  g_string_append(input, "2X");
  _put_u32(input, 1);
  _put_u32(input, 1);
  _assert_protocol_error(input, "Unknown Lumberjack frame type");
}

/****************************************************************************
 * Compressed frames (specification 9)
 ****************************************************************************/

static void
_assert_compressed_window_delivered(LumberjackTestConnection *conn, guint32 size, const gchar *prefix)
{
  cr_assert_eq(_pump_until(conn, 0, size), LUMBERJACK_PUMP_MESSAGES);
  for (guint32 i = 1; i <= size; i++)
    {
      gchar *payload = g_strdup_printf("{\"message\":\"%s-%u\"}", prefix, i);

      _assert_message(conn, i - 1, payload, "2");
      g_free(payload);
    }
}

Test(lumberjack, compressed_window_is_inflated_and_acknowledged)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  /* level 3 is what Beats and go-lumber use by default (9.3) */
  _inject(&conn, _compressed_window_of_json(3, "c", 3));
  _assert_compressed_window_delivered(&conn, 3, "c");

  _assert_nothing_written(&conn);
  _report_durable(&conn, 2);
  _assert_written(&conn, _ack('2', 3));
  _connection_deinit(&conn);
}

/* the byte-exact vector of Appendix B.6: W(2) and a `C` frame of two `J`
 * frames deflated at level 6 */
Test(lumberjack, appendix_b6_compressed_frame_vector)
{
  static const guchar vector[] =
  {
    0x32, 0x57, 0x00, 0x00, 0x00, 0x02,
    0x32, 0x43, 0x00, 0x00, 0x00, 0x27,
    0x78, 0x9c, 0x33, 0xf2, 0x62, 0x60, 0x60, 0x60, 0x04, 0x62, 0xce, 0x6a, 0xa5, 0x6c, 0x25, 0x2b,
    0xa5, 0x32, 0xa5, 0x5a, 0x23, 0x90, 0x10, 0x13, 0x10, 0xb3, 0x57, 0x2b, 0xe5, 0x29, 0x59, 0x19,
    0xd6, 0x02, 0x00, 0x5c, 0x36, 0x05, 0xbc,
  };
  LumberjackTestConnection conn;
  GString *input = g_string_new_len((const gchar *) vector, sizeof(vector));

  _connection_init(&conn);
  _inject(&conn, input);
  cr_assert_eq(_pump_until(&conn, 0, 2), LUMBERJACK_PUMP_MESSAGES);
  _assert_message(&conn, 0, "{\"k\":\"v\"}", "2");
  _assert_message(&conn, 1, "{\"n\":1}", "2");
  _report_durable(&conn, 1);
  _assert_written(&conn, _ack('2', 2));
  _connection_deinit(&conn);
}

Test(lumberjack, every_compression_level_is_accepted)
{
  static const gint levels[] = { 1, 6, 9 };

  for (gsize i = 0; i < G_N_ELEMENTS(levels); i++)
    {
      LumberjackTestConnection conn;

      _connection_init(&conn);
      _inject(&conn, _compressed_window_of_json(4, "lvl", levels[i]));
      _assert_compressed_window_delivered(&conn, 4, "lvl");
      _report_durable(&conn, 3);
      _assert_written(&conn, _ack('2', 4));
      _connection_deinit(&conn);
    }
}

/* logstash-forwarder always compressed, so a version 1 window is `W` and
 * one `C` frame of `D` frames */
Test(lumberjack, version1_data_frames_inside_a_compressed_frame)
{
  LumberjackTestConnection conn;
  GString *frames = g_string_new("");
  GString *input = g_string_new("");

  _put_data_frame(frames, 1, 2, "line", "first", "host", "web");
  _put_data_frame(frames, 2, 1, "line", "second");
  _put_window(input, '1', 2);
  _put_compressed(input, '1', frames, 3);
  g_string_free(frames, TRUE);

  _connection_init(&conn);
  _inject(&conn, input);
  cr_assert_eq(_pump_until(&conn, 0, 2), LUMBERJACK_PUMP_MESSAGES);
  _assert_message(&conn, 0, "{\"line\":\"first\",\"host\":\"web\"}", "1");
  _assert_message(&conn, 1, "{\"line\":\"second\"}", "1");
  _report_durable(&conn, 1);
  _assert_written(&conn, _ack('1', 2));
  _connection_deinit(&conn);
}

Test(lumberjack, plain_and_compressed_frames_mixed_in_a_window)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  GString *middle = _json_frames(2, 1, "mix");

  _put_window(input, '2', 3);
  _put_json_frame(input, 1, "{\"message\":\"mix-1\"}");
  _put_compressed(input, '2', middle, 6);
  _put_json_frame(input, 3, "{\"message\":\"mix-3\"}");
  g_string_free(middle, TRUE);

  _connection_init(&conn);
  _inject(&conn, input);
  _assert_compressed_window_delivered(&conn, 3, "mix");
  _report_durable(&conn, 2);
  _assert_written(&conn, _ack('2', 3));
  _connection_deinit(&conn);
}

Test(lumberjack, two_compressed_frames_in_a_window)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");
  GString *first = _json_frames(1, 2, "two");
  GString *second = _json_frames(3, 2, "two");

  _put_window(input, '2', 4);
  _put_compressed(input, '2', first, 6);
  _put_compressed(input, '2', second, 6);
  g_string_free(first, TRUE);
  g_string_free(second, TRUE);

  _connection_init(&conn);
  _inject(&conn, input);
  _assert_compressed_window_delivered(&conn, 4, "two");
  _report_durable(&conn, 3);
  _assert_written(&conn, _ack('2', 4));
  _connection_deinit(&conn);
}

Test(lumberjack, compressed_frame_arriving_one_octet_at_a_time_is_inflated)
{
  LumberjackTestConnection conn;

  _connection_init_stream(&conn, _compressed_window_of_json(3, "slow", 6));
  _assert_compressed_window_delivered(&conn, 3, "slow");
  _report_durable(&conn, 2);
  _assert_written(&conn, _ack('2', 3));
  _connection_deinit(&conn);
}

Test(lumberjack, compressed_windows_pipelined_are_acknowledged_in_order)
{
  LumberjackTestConnection conn;
  GString *expected = _ack('2', 2);
  GString *second = _ack('2', 3);

  _connection_init(&conn);
  _inject(&conn, _compressed_window_of_json(2, "a", 3));
  _inject(&conn, _compressed_window_of_json(3, "b", 3));
  cr_assert_eq(_pump_until(&conn, 0, 5), LUMBERJACK_PUMP_MESSAGES);
  _assert_message(&conn, 4, "{\"message\":\"b-3\"}", "2");

  _report_durable(&conn, 4);
  g_string_append_len(expected, second->str, second->len);
  g_string_free(second, TRUE);
  _assert_written(&conn, expected);
  _connection_deinit(&conn);
}

Test(lumberjack, flow_control_window_full_inside_a_compressed_frame_resumes)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _inject(&conn, _compressed_window_of_json(3, "fc", 3));
  cr_assert_eq(_pump_until(&conn, 0, 1), LUMBERJACK_PUMP_MESSAGES);

  conn.window_full = TRUE;
  cr_assert_eq(_pump(&conn), LUMBERJACK_PUMP_IDLE);
  cr_assert_eq(conn.messages->len, 1);

  conn.window_full = FALSE;
  _assert_compressed_window_delivered(&conn, 3, "fc");
  _report_durable(&conn, 2);
  _assert_written(&conn, _ack('2', 3));
  _connection_deinit(&conn);
}

/* a frame that inflates to far more than log-msg-size() is skipped as it is
 * inflated and never buffered, then counted toward the acknowledgement */
Test(lumberjack, oversize_frame_inside_a_compressed_frame_is_skipped_and_counted)
{
  LumberjackTestConnection conn;
  GString *frames = g_string_new("");
  GString *input = g_string_new("");
  gsize bomb_len = 4 * 1024 * 1024;
  gchar *bomb = g_malloc(bomb_len);

  memset(bomb, 'z', bomb_len);
  _put_json_frame(frames, 1, "{\"n\":1}");
  _put_json_frame_len(frames, 2, bomb, bomb_len);
  _put_json_frame(frames, 3, "{\"n\":3}");
  g_free(bomb);
  _put_window(input, '2', 3);
  _put_compressed(input, '2', frames, 9);
  g_string_free(frames, TRUE);

  _connection_init(&conn);
  options_storage.super.max_msg_size = 100;
  cr_assert_lt(input->len, (gsize) 16384, "the bomb did not compress, the test is void");

  start_grabbing_messages();
  _inject(&conn, input);
  cr_assert_eq(_pump_until(&conn, 0, 2), LUMBERJACK_PUMP_MESSAGES);
  stop_grabbing_messages();
  assert_grabbed_log_contains("Dropping a message larger than log-msg-size()");

  _assert_message(&conn, 0, "{\"n\":1}", "2");
  _assert_message(&conn, 1, "{\"n\":3}", "2");
  _report_durable(&conn, 1);
  _assert_written(&conn, _ack('2', 3));
  _connection_deinit(&conn);
}

/* the canonical receiver drains what follows the zlib stream within the
 * declared length (9.4) */
Test(lumberjack, padding_after_the_zlib_stream_is_discarded)
{
  LumberjackTestConnection conn;
  GString *frames = _json_frames(1, 2, "pad");
  GString *compressed = g_string_new("");
  GString *input = g_string_new("");

  _put_compressed(compressed, '2', frames, 6);
  g_string_free(frames, TRUE);
  /* raise the declared length by the padding and append it */
  guint32 declared = _read_u32_from((const guchar *) compressed->str + 2);
  _put_window(input, '2', 2);
  g_string_append_len(input, compressed->str, 2);
  _put_u32(input, declared + 5);
  g_string_append_len(input, compressed->str + 6, compressed->len - 6);
  g_string_append(input, "XXXXX");
  g_string_free(compressed, TRUE);
  /* and a plain window after it proves the socket input stayed aligned */
  _put_window(input, '2', 1);
  _put_json_frame(input, 1, "{\"after\":true}");

  _connection_init(&conn);
  _inject(&conn, input);
  cr_assert_eq(_pump_until(&conn, 0, 3), LUMBERJACK_PUMP_MESSAGES);
  _assert_message(&conn, 2, "{\"after\":true}", "2");
  _report_durable(&conn, 2);
  GString *expected = _ack('2', 2);
  GString *second = _ack('2', 1);
  g_string_append_len(expected, second->str, second->len);
  g_string_free(second, TRUE);
  _assert_written(&conn, expected);
  _connection_deinit(&conn);
}

Test(lumberjack, truncated_zlib_stream_is_a_protocol_error)
{
  GString *frames = _json_frames(1, 2, "trunc");
  GString *compressed = g_string_new("");
  GString *input = g_string_new("");

  _put_compressed(compressed, '2', frames, 6);
  g_string_free(frames, TRUE);
  /* declare and send four octets less: the Adler-32 trailer is cut off */
  guint32 declared = _read_u32_from((const guchar *) compressed->str + 2);
  _put_window(input, '2', 2);
  g_string_append_len(input, compressed->str, 2);
  _put_u32(input, declared - 4);
  g_string_append_len(input, compressed->str + 6, compressed->len - 6 - 4);
  g_string_free(compressed, TRUE);
  /* the next window must not be mistaken for the rest of the stream */
  _put_window(input, '2', 1);
  _put_json_frame(input, 1, "{}");

  _assert_protocol_error(input, "compressed frame ends before its zlib stream does");
}

Test(lumberjack, compressed_frame_of_zero_length_is_a_protocol_error)
{
  GString *input = g_string_new("");

  _put_window(input, '2', 1);
  g_string_append(input, "2C");
  _put_u32(input, 0);
  _assert_protocol_error(input, "compressed frame ends before its zlib stream does");
}

Test(lumberjack, corrupt_checksum_is_a_protocol_error)
{
  GString *frames = _json_frames(1, 2, "adler");
  GString *input = g_string_new("");

  _put_window(input, '2', 2);
  _put_compressed(input, '2', frames, 6);
  g_string_free(frames, TRUE);
  input->str[input->len - 1] ^= 0xff;

  _assert_protocol_error(input, "the zlib stream is corrupt");
}

Test(lumberjack, garbage_instead_of_a_zlib_stream_is_a_protocol_error)
{
  GString *input = g_string_new("");

  /* the negative vector of Appendix B.8: a `00 00` zlib header */
  _put_window(input, '2', 1);
  g_string_append(input, "2C");
  _put_u32(input, 5);
  g_string_append_len(input, "\0\0\0\0\0", 5);
  _assert_protocol_error(input, "the zlib stream is corrupt");
}

Test(lumberjack, frame_straddling_the_end_of_a_compressed_frame_is_a_protocol_error)
{
  GString *frames = _json_frames(1, 1, "straddle");
  GString *input = g_string_new("");

  /* the second frame is cut after its header, the rest would follow plain */
  gsize whole = frames->len;
  _put_json_frame(frames, 2, "{\"message\":\"straddle-2\"}");
  g_string_truncate(frames, whole + 12);
  _put_window(input, '2', 2);
  _put_compressed(input, '2', frames, 6);
  g_string_free(frames, TRUE);

  _assert_protocol_error(input, "frame straddles the end of a compressed frame");
}

Test(lumberjack, window_frame_inside_a_compressed_frame_is_a_protocol_error)
{
  GString *frames = g_string_new("");
  GString *input = g_string_new("");

  _put_window(frames, '2', 1);
  _put_json_frame(frames, 1, "{}");
  _put_window(input, '2', 1);
  _put_compressed(input, '2', frames, 6);
  g_string_free(frames, TRUE);

  _assert_protocol_error(input, "window frame inside a compressed frame");
}

Test(lumberjack, nested_compressed_frame_is_a_protocol_error)
{
  GString *inner = _json_frames(1, 1, "nested");
  GString *outer = g_string_new("");
  GString *input = g_string_new("");

  _put_compressed(outer, '2', inner, 6);
  _put_window(input, '2', 1);
  _put_compressed(input, '2', outer, 6);
  g_string_free(inner, TRUE);
  g_string_free(outer, TRUE);

  _assert_protocol_error(input, "compressed frame inside a compressed frame");
}

Test(lumberjack, frames_beyond_the_window_inside_a_compressed_frame_are_a_protocol_error)
{
  GString *frames = _json_frames(1, 2, "extra");
  GString *input = g_string_new("");

  _put_window(input, '2', 1);
  _put_compressed(input, '2', frames, 6);
  g_string_free(frames, TRUE);

  _assert_protocol_error(input, "carries data after the last frame of the window");
}

Test(lumberjack, compressed_frame_version_differing_from_the_window_is_a_protocol_error)
{
  GString *frames = _json_frames(1, 1, "v");
  GString *input = g_string_new("");

  _put_window(input, '1', 1);
  _put_compressed(input, '2', frames, 6);
  g_string_free(frames, TRUE);

  _assert_protocol_error(input, "frame version differs from the version of its window");
}

/****************************************************************************
 * Keepalive (specification 10)
 ****************************************************************************/

Test(lumberjack, keepalive_is_sent_while_a_version2_window_waits)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _inject(&conn, _window_of_json(1, "m"));
  _pump_until(&conn, 0, 1);

  log_proto_lumberjack_server_fire_keepalive(conn.proto);
  _assert_written(&conn, _ack('2', 0));

  /* the keepalive changes nothing about the window itself */
  _report_durable(&conn, 0);
  GString *expected = _ack('2', 0);
  GString *ack = _ack('2', 1);
  g_string_append_len(expected, ack->str, ack->len);
  g_string_free(ack, TRUE);
  _assert_written(&conn, expected);
  _connection_deinit(&conn);
}

Test(lumberjack, no_keepalive_for_a_version1_window)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _put_window(input, '1', 1);
  _put_data_frame(input, 1, 1, "a", "b");

  _connection_init(&conn);
  _inject(&conn, input);
  _pump_until(&conn, 0, 1);

  log_proto_lumberjack_server_fire_keepalive(conn.proto);
  _assert_nothing_written(&conn);
  _connection_deinit(&conn);
}

Test(lumberjack, no_keepalive_without_a_pending_window)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _pump(&conn);
  log_proto_lumberjack_server_fire_keepalive(conn.proto);
  _assert_nothing_written(&conn);
  _connection_deinit(&conn);
}

Test(lumberjack, keepalive_is_disabled_by_interval_zero)
{
  LumberjackTestConnection conn;

  _connection_init_with(&conn, "lumberjack(keepalive-interval(0))");
  _inject(&conn, _window_of_json(1, "m"));
  _pump_until(&conn, 0, 1);

  log_proto_lumberjack_server_fire_keepalive(conn.proto);
  _assert_nothing_written(&conn);
  _connection_deinit(&conn);
}

/****************************************************************************
 * poll_prepare: timeouts and readiness
 ****************************************************************************/

Test(lumberjack, waiting_for_a_window_uses_the_idle_timeout_and_a_window_body_the_window_timeout)
{
  LumberjackTestConnection conn;
  GString *input = g_string_new("");

  _connection_init(&conn);
  options_storage.super.idle_timeout = 600;
  _pump(&conn);
  cr_assert_eq(conn.cond, G_IO_IN);
  cr_assert_eq(conn.timeout, 600, "between windows idle-timeout() applies, got %d", conn.timeout);

  _put_window(input, '2', 2);
  _put_json_frame(input, 1, "{}");
  _inject(&conn, input);
  _pump(&conn);
  cr_assert_eq(conn.messages->len, 1);
  cr_assert_eq(conn.timeout, LUMBERJACK_DEFAULT_WINDOW_TIMEOUT,
               "inside a window window-timeout() applies, got %d", conn.timeout);
  _connection_deinit(&conn);
}

Test(lumberjack, buffered_input_asks_for_an_immediate_fetch)
{
  LumberjackTestConnection conn;
  GIOCondition cond = 0;
  gint timeout = -1;

  _connection_init(&conn);
  _pump(&conn);
  _inject(&conn, _window_of_json(2, "m"));
  /* the first fetch reads everything and delivers the first frame */
  _pump_until(&conn, 0, 1);
  cr_assert_eq(log_proto_server_poll_prepare(conn.proto, &cond, &timeout), LPPA_FORCE_SCHEDULE_FETCH,
               "a buffered frame needs no poll");
  _connection_deinit(&conn);
}

/****************************************************************************
 * TLS (specification 15.2)
 ****************************************************************************/

Test(lumberjack, tls_is_started_before_the_first_read_when_configured)
{
  LumberjackTestConnection conn;

  _connection_init_full(&conn, "lumberjack()", NULL, FALSE, TRUE);
  cr_assert_neq(conn.proto->transport_stack.active_transport, LOG_TRANSPORT_TLS);

  _pump(&conn);
  cr_assert_eq(conn.proto->transport_stack.active_transport, LOG_TRANSPORT_TLS);

  _inject(&conn, _window_of_json(1, "tls"));
  _pump_until(&conn, 0, 1);
  _assert_message(&conn, 0, "{\"message\":\"tls-1\"}", "2");
  _report_durable(&conn, 0);
  _assert_written(&conn, _ack('2', 1));
  _connection_deinit(&conn);
}

Test(lumberjack, plaintext_without_tls_configured)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _pump(&conn);
  cr_assert_eq(conn.proto->transport_stack.active_transport, LOG_TRANSPORT_INITIAL);
  _connection_deinit(&conn);
}

/****************************************************************************
 * Bookmarks outliving the connection
 ****************************************************************************/

Test(lumberjack, a_bookmark_saved_after_the_connection_is_gone_is_harmless)
{
  LumberjackTestConnection conn;

  _connection_init(&conn);
  _inject(&conn, _window_of_json(1, "late"));
  _pump_until(&conn, 0, 1);

  Bookmark *bookmark = (Bookmark *) g_ptr_array_steal_index(conn.bookmarks, 0);
  _connection_deinit(&conn);

  bookmark_save(bookmark);
  _free_bookmark(bookmark);
}

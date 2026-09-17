/*
 * Copyright (c) 2012-2019 Balabit
 * Copyright (c) 2012 Balázs Scheidler
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
#include "proto_lib.h"
#include "grab-logging.h"

#include "cfg.h"
#include "messages.h"
#include <string.h>

LogProtoServerOptions proto_server_options;

void
assert_proto_server_status(LogProtoServer *proto, LogProtoStatus status, LogProtoStatus expected_status)
{
  cr_assert_eq(status, expected_status, "LogProtoServer expected status mismatch");
}

/* One fetch(); a replacement the proto asks for is applied and reported in
 * @replaced, starting the replacement with a fresh @may_read like the reader's
 * next pass does, and LPS_AGAIN reads as LPS_SUCCESS. */
static LogProtoStatus
_fetch_once(LogProtoServer **proto, const guchar **msg, gsize *msg_len, gboolean *may_read,
            LogTransportAuxData *aux, Bookmark *bookmark, gboolean *replaced)
{
  LogProtoStatus status = log_proto_server_fetch(*proto, msg, msg_len, may_read, aux, bookmark);

  *replaced = FALSE;
  if (status == LPS_AGAIN)
    {
      *replaced = log_proto_server_apply_replacement(proto);
      if (*replaced)
        *may_read = TRUE;
      status = LPS_SUCCESS;
    }
  return status;
}

/* Fetch until a message is produced, so that logproto-auto-server is
 * transparent here. */
LogProtoStatus
proto_server_fetch(LogProtoServer **proto, const guchar **msg, gsize *msg_len)
{
  Bookmark bookmark;
  LogTransportAuxData aux = {0};
  GSockAddr *saddr;
  gboolean may_read = TRUE;
  gboolean replaced;
  LogProtoStatus status;

  start_grabbing_messages();
  log_transport_aux_data_init(&aux);
  do
    {
      log_transport_aux_data_reinit(&aux);
      status = _fetch_once(proto, msg, msg_len, &may_read, &aux, &bookmark, &replaced);
    }
  while (status == LPS_SUCCESS && *msg == NULL && may_read);

  saddr = aux.peer_addr;
  if (status != LPS_SUCCESS)
    cr_assert_null(saddr, "returned saddr must be NULL on failure");

  log_transport_aux_data_destroy(&aux);

  stop_grabbing_messages();
  return status;
}

LogProtoServer *
construct_server_proto_plugin(const gchar *name, LogTransport *transport)
{
  LogProtoServerFactory *proto_factory;

  log_proto_server_options_init(&proto_server_options, configuration);
  proto_factory = log_proto_server_get_factory(&configuration->plugin_context, name);
  cr_assert_not_null(proto_factory, "error looking up proto factory");
  return log_proto_server_factory_construct(proto_factory, transport, &proto_server_options, NULL);
}

/* Assert that *proto replaces itself and apply the replacement.
 * @detect_message, when given, pins down which proto was chosen. */
void
assert_proto_server_replacement(LogProtoServer **proto, const gchar *detect_message)
{
  gboolean replaced = FALSE;
  const guchar *msg = NULL;
  gsize msg_len = 0;
  LogTransportAuxData aux = {0};
  Bookmark bookmark;
  gboolean may_read = TRUE;
  LogProtoStatus status;
  gint saved_debug_flag = debug_flag;

  debug_flag = TRUE;
  start_grabbing_messages();
  log_transport_aux_data_init(&aux);
  do
    {
      log_transport_aux_data_reinit(&aux);
      status = _fetch_once(proto, &msg, &msg_len, &may_read, &aux, &bookmark, &replaced);
      cr_assert_null(msg, "a proto that replaces itself must not return a message");
    }
  while (status == LPS_SUCCESS && !replaced && may_read);
  log_transport_aux_data_destroy(&aux);
  stop_grabbing_messages();
  debug_flag = saved_debug_flag;

  assert_proto_server_status(*proto, status, LPS_SUCCESS);
  if (detect_message)
    assert_grabbed_log_contains(detect_message);
}

void
assert_proto_server_fetch(LogProtoServer **proto, const gchar *expected_msg, gssize expected_msg_len)
{
  const guchar *msg = NULL;
  gsize msg_len = 0;
  LogProtoStatus status;

  status = proto_server_fetch(proto, &msg, &msg_len);

  assert_proto_server_status(*proto, status, LPS_SUCCESS);

  if (expected_msg_len < 0)
    expected_msg_len = strlen(expected_msg);

  cr_assert_eq(msg_len, expected_msg_len, "LogProtoServer expected message mismatch (length) "
                                          "actual: %" G_GSIZE_FORMAT " expected: %" G_GSIZE_FORMAT, msg_len, expected_msg_len);
  cr_assert_arr_eq((const gchar *) msg, expected_msg, expected_msg_len,
                   "LogProtoServer expected message mismatch");
}

void
assert_proto_server_fetch_single_read(LogProtoServer **proto, const gchar *expected_msg, gssize expected_msg_len)
{
  const guchar *msg = NULL;
  gsize msg_len = 0;
  LogProtoStatus status;
  LogTransportAuxData aux = {0};
  Bookmark bookmark;
  gboolean may_read = TRUE;
  gboolean replaced;

  start_grabbing_messages();
  log_transport_aux_data_init(&aux);
  status = _fetch_once(proto, &msg, &msg_len, &may_read, &aux, &bookmark, &replaced);
  assert_proto_server_status(*proto, status, LPS_SUCCESS);

  if (expected_msg)
    {
      if (expected_msg_len < 0)
        expected_msg_len = strlen(expected_msg);

      cr_assert_eq(msg_len, expected_msg_len, "LogProtoServer expected message mismatch (length)");
      cr_assert_arr_eq((const gchar *) msg, expected_msg, expected_msg_len,
                       "LogProtoServer expected message mismatch");
    }
  else
    {
      cr_assert_null(msg, "when single-read finds an incomplete message, msg must be NULL");
      cr_assert_null(aux.peer_addr, "returned saddr must be NULL on success");
    }

  log_transport_aux_data_destroy(&aux);
  stop_grabbing_messages();
}

void
assert_proto_server_fetch_failure(LogProtoServer **proto, LogProtoStatus expected_status, const gchar *error_message)
{
  const guchar *msg = NULL;
  gsize msg_len = 0;
  LogProtoStatus status;

  status = proto_server_fetch(proto, &msg, &msg_len);

  assert_proto_server_status(*proto, status, expected_status);
  if (error_message)
    assert_grabbed_log_contains(error_message);
}


void
assert_proto_server_fetch_ignored_eof(LogProtoServer **proto)
{
  const guchar *msg = NULL;
  gsize msg_len = 0;
  LogProtoStatus status;
  LogTransportAuxData aux = {0};
  Bookmark bookmark;
  gboolean may_read = TRUE;
  gboolean replaced;

  start_grabbing_messages();
  log_transport_aux_data_init(&aux);
  status = _fetch_once(proto, &msg, &msg_len, &may_read, &aux, &bookmark, &replaced);
  assert_proto_server_status(*proto, status, LPS_SUCCESS);
  cr_assert_null(msg, "when an EOF is ignored msg must be NULL");
  cr_assert_null(aux.peer_addr, "returned saddr must be NULL on success");
  log_transport_aux_data_destroy(&aux);
  stop_grabbing_messages();
}

void
init_proto_tests(void)
{
  configuration = cfg_new_snippet();
  log_proto_server_options_defaults(&proto_server_options);
}

void
deinit_proto_tests(void)
{
  log_proto_server_options_destroy(&proto_server_options);

  if (configuration)
    cfg_free(configuration);
}

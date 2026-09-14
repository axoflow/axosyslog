/*
 * Copyright (c) 2012-2019 Balabit
 * Copyright (c) 2012-2013 Balázs Scheidler
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
#include "libtest/msg_parse_lib.h"

#include "logproto/logproto-auto-server.h"

#include <errno.h>

/* a single fetch that returned neither a message nor a replacement */
static void
assert_detection_pending(LogProtoServer *proto)
{
  const guchar *msg = NULL;
  gsize msg_len = 0;
  LogTransportAuxData aux = {0};
  Bookmark bookmark;
  gboolean may_read = TRUE;
  LogProtoStatus status;

  log_transport_aux_data_init(&aux);
  status = log_proto_server_fetch(proto, &msg, &msg_len, &may_read, &aux, &bookmark);
  log_transport_aux_data_destroy(&aux);

  assert_proto_server_status(proto, status, LPS_AGAIN);
  cr_assert_null(msg, "the auto server must never return a message of its own");
  cr_assert_null(proto->proto_replacement,
                 "detection must not conclude before it has enough data to tell the framing apart");
}

Test(log_proto, test_log_proto_initial_framing_too_long)
{
  LogProtoServer *proto;

  proto = log_proto_auto_server_new(
            log_transport_mock_stream_new(
              "100000000 too long\n", -1,
              LTM_EOF),
            get_inited_proto_server_options(),
            NULL);

  assert_proto_server_replacement(&proto, "Auto-detected octet-counted-framing");
  assert_proto_server_fetch_failure(&proto, LPS_ERROR, NULL);
  log_proto_server_free(proto);
}

Test(log_proto, test_log_proto_error_in_initial_frame)
{
  LogProtoServer *proto;

  proto = log_proto_auto_server_new(
            log_transport_mock_stream_new(
              LTM_INJECT_ERROR(EIO)),
            get_inited_proto_server_options(),
            NULL);

  assert_proto_server_fetch_failure(&proto, LPS_ERROR, NULL);
  log_proto_server_free(proto);
}

Test(log_proto, test_log_proto_eof_before_detection)
{
  LogProtoServer *proto;

  proto = log_proto_auto_server_new(
            log_transport_mock_stream_new(
              LTM_EOF),
            get_inited_proto_server_options(),
            NULL);

  assert_proto_server_fetch_failure(&proto, LPS_EOF, NULL);
  log_proto_server_free(proto);
}

Test(log_proto, test_log_proto_auto_server_no_framing)
{
  LogProtoServer *proto;

  proto = log_proto_auto_server_new(
            log_transport_mock_stream_new(
              "abcdefghijklmnopqstuvwxyz\n", -1,
              "01234567\n", -1,
              "01234567\0", 9,
              "abcdef", -1,
              LTM_EOF),
            get_inited_proto_server_options(),
            NULL);

  assert_proto_server_replacement(&proto, "Unable to detect framing");
  assert_proto_server_fetch(&proto, "abcdefghijklmnopqstuvwxyz", -1);
  assert_proto_server_fetch(&proto, "01234567", -1);
  assert_proto_server_fetch(&proto, "01234567", 8);
  assert_proto_server_fetch(&proto, "abcdef", -1);
  assert_proto_server_fetch_failure(&proto, LPS_EOF, NULL);
  log_proto_server_free(proto);
}

Test(log_proto, test_log_proto_auto_server_opening_bracket)
{
  LogProtoServer *proto;

  proto = log_proto_auto_server_new(
            log_transport_mock_stream_new(
              "<55> abcdefghijklmnopqstuvwxyz\n", -1,
              "01234567\n", -1,
              "01234567\0", 9,
              "abcdef", -1,
              LTM_EOF),
            get_inited_proto_server_options(),
            NULL);

  assert_proto_server_replacement(&proto, "Auto-detected non-transparent-framing");
  assert_proto_server_fetch(&proto, "<55> abcdefghijklmnopqstuvwxyz", -1);
  assert_proto_server_fetch(&proto, "01234567", -1);
  assert_proto_server_fetch(&proto, "01234567", 8);
  assert_proto_server_fetch(&proto, "abcdef", -1);
  assert_proto_server_fetch_failure(&proto, LPS_EOF, NULL);
  log_proto_server_free(proto);
}

Test(log_proto, test_log_proto_auto_server_with_framing)
{
  LogProtoServer *proto;

  proto = log_proto_auto_server_new(
            log_transport_mock_stream_new(
              "32 0123456789ABCDEF0123456789ABCDEF", -1,
              "10 01234567\n\n", -1,
              "10 01234567\0\0", 13,
              /* utf8 */
              "30 árvíztűrőtükörfúrógép", -1,
              /* iso-8859-2 */
              "21 \xe1\x72\x76\xed\x7a\x74\xfb\x72\xf5\x74\xfc\x6b\xf6\x72\x66\xfa"      /*  |árvíztűrőtükörfú| */
              "\x72\xf3\x67\xe9\x70", -1,                                                /*  |rógép|            */
              /* ucs4 */
              "32 \x00\x00\x00\xe1\x00\x00\x00\x72\x00\x00\x00\x76\x00\x00\x00\xed"      /* |...á...r...v...í| */
              "\x00\x00\x00\x7a\x00\x00\x00\x74\x00\x00\x01\x71\x00\x00\x00\x72", 35,    /* |...z...t...ű...r|  */
              LTM_EOF),
            get_inited_proto_server_options(),
            NULL);

  /* the mock hands out one byte per read, so the first fetch only sees "3" */
  assert_detection_pending(proto);
  assert_proto_server_replacement(&proto, "Auto-detected octet-counted-framing");
  assert_proto_server_fetch(&proto, "0123456789ABCDEF0123456789ABCDEF", -1);
  assert_proto_server_fetch(&proto, "01234567\n\n", -1);
  assert_proto_server_fetch(&proto, "01234567\0\0", 10);
  assert_proto_server_fetch(&proto, "árvíztűrőtükörfúrógép", -1);
  assert_proto_server_fetch(&proto,
                            "\xe1\x72\x76\xed\x7a\x74\xfb\x72\xf5\x74\xfc\x6b\xf6\x72\x66\xfa"        /*  |.rv.zt.r.t.k.rf.| */
                            "\x72\xf3\x67\xe9\x70", -1);                                              /*  |r.g.p|            */
  assert_proto_server_fetch(&proto,
                            "\x00\x00\x00\xe1\x00\x00\x00\x72\x00\x00\x00\x76\x00\x00\x00\xed"        /* |...á...r...v...í| */
                            "\x00\x00\x00\x7a\x00\x00\x00\x74\x00\x00\x01\x71\x00\x00\x00\x72", 32);  /* |...z...t...q...r|  */
  assert_proto_server_fetch_failure(&proto, LPS_EOF, NULL);
  log_proto_server_free(proto);
}

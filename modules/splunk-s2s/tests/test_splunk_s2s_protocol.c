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

#include "splunk-s2s-protocol.h"
#include "test_splunk_s2s_protocol_vectors.h"

#include <string.h>

static void
_assert_bytes_eq(const GString *actual, const guint8 *expected, gsize expected_len)
{
  for (gsize i = 0; i < MIN(actual->len, expected_len); i++)
    {
      cr_assert_eq((guint8) actual->str[i], expected[i],
                   "byte mismatch at offset %" G_GSIZE_FORMAT ": actual 0x%02x, expected 0x%02x",
                   i, (guint8) actual->str[i], expected[i]);
    }
  cr_assert_eq(actual->len, expected_len,
               "encoded length mismatch: actual %" G_GSIZE_FORMAT ", expected %" G_GSIZE_FORMAT,
               actual->len, expected_len);
}

Test(splunk_s2s_protocol, varint_edge_values)
{
  GString *out = g_string_new(NULL);

  splunk_s2s_write_varint(out, 0);
  _assert_bytes_eq(out, vector_varint_0, VECTOR_LEN(vector_varint_0));

  g_string_truncate(out, 0);
  splunk_s2s_write_varint(out, 127);
  _assert_bytes_eq(out, vector_varint_127, VECTOR_LEN(vector_varint_127));

  g_string_truncate(out, 0);
  splunk_s2s_write_varint(out, 128);
  _assert_bytes_eq(out, vector_varint_128, VECTOR_LEN(vector_varint_128));

  g_string_truncate(out, 0);
  splunk_s2s_write_varint(out, 300);
  _assert_bytes_eq(out, vector_varint_300, VECTOR_LEN(vector_varint_300));

  g_string_truncate(out, 0);
  splunk_s2s_write_varint(out, G_MAXUINT32);
  _assert_bytes_eq(out, vector_varint_u32_max, VECTOR_LEN(vector_varint_u32_max));

  g_string_truncate(out, 0);
  splunk_s2s_write_varint(out, G_MAXUINT64);
  _assert_bytes_eq(out, vector_varint_u64_max, VECTOR_LEN(vector_varint_u64_max));

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, header1)
{
  GString *out = g_string_new(NULL);

  cr_assert(splunk_s2s_format_header1(out, "axosyslog", "0"));
  cr_assert_eq(out->len, SPLUNK_S2S_HEADER1_SIZE);
  _assert_bytes_eq(out, vector_header1, sizeof(vector_header1));

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, header1_rejects_overlong_fields)
{
  GString *out = g_string_new(NULL);
  gchar overlong[SPLUNK_S2S_HEADER1_SIZE];

  memset(overlong, 'x', sizeof(overlong));

  overlong[SPLUNK_S2S_HEADER1_MAX_IDENTIFIER_LEN + 1] = '\0';
  cr_assert_not(splunk_s2s_format_header1(out, overlong, "0"));

  overlong[SPLUNK_S2S_HEADER1_MAX_MGMT_PORT_LEN + 1] = '\0';
  cr_assert_not(splunk_s2s_format_header1(out, "axosyslog", overlong));

  cr_assert_eq(out->len, 0);

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, v3_signature_frame)
{
  GString *out = g_string_new(NULL);

  splunk_s2s_format_v3_signature_frame(out, SPLUNK_S2S_CAPABILITIES_SIGNATURE);
  _assert_bytes_eq(out, vector_v3_signature_frame, VECTOR_LEN(vector_v3_signature_frame));

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, v3_forwarder_info_frame)
{
  GString *out = g_string_new(NULL);

  splunk_s2s_format_v3_forwarder_info_frame(out, "ForwarderInfo test", "TESTGUID-1234", 1750000000);
  _assert_bytes_eq(out, vector_v3_forwarder_info_frame, VECTOR_LEN(vector_v3_forwarder_info_frame));

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, open_channel)
{
  GString *out = g_string_new(NULL);

  splunk_s2s_format_open_channel(out, 1, "/var/log/messages", "testhost", "syslog");
  _assert_bytes_eq(out, vector_open_channel, VECTOR_LEN(vector_open_channel));

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, event_full_header)
{
  GString *out = g_string_new(NULL);
  const gchar *raw = "Jul  2 15:04:05 testhost app[123]: árvíztűrő tükörfúrógép";
  SplunkS2SEventField fields[] =
  {
    { .name = "_MetaData:Index", .value_type = SPLUNK_S2S_VALUE_STR, .str_value = "main", .str_value_len = 4 },
    { .name = "count", .value_type = SPLUNK_S2S_VALUE_NUMBER, .number_value = 42 },
  };

  splunk_s2s_format_event(out, 1, SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER, 1750000000, 0,
                          fields, G_N_ELEMENTS(fields), raw, strlen(raw));
  _assert_bytes_eq(out, vector_event_full_header, VECTOR_LEN(vector_event_full_header));

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, event_short_header_ignores_timestamp_and_takes_empty_payload)
{
  GString *out = g_string_new(NULL);
  SplunkS2SEventField fields[] =
  {
    { .name = "_MetaData:Index", .value_type = SPLUNK_S2S_VALUE_STR, .str_value = "main", .str_value_len = 4 },
  };

  splunk_s2s_format_event(out, 3, SPLUNK_S2S_EVENT_FLAGS_SHORT_HEADER, 1750000000, 0,
                          fields, G_N_ELEMENTS(fields), "", 0);
  _assert_bytes_eq(out, vector_event_short_header, VECTOR_LEN(vector_event_short_header));

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, close_channel)
{
  GString *out = g_string_new(NULL);

  splunk_s2s_format_close_channel(out, 7);
  _assert_bytes_eq(out, vector_close_channel, VECTOR_LEN(vector_close_channel));

  g_string_free(out, TRUE);
}

Test(splunk_s2s_protocol, parse_v3_frame_len)
{
  guint32 frame_len = 0;
  const guint8 buf[] =
    "\x00\x00\x01\xd7"  /* frame length 471 */
    "\xde\xad";         /* the first body bytes */

  cr_assert(splunk_s2s_parse_v3_frame_len(buf, VECTOR_LEN(buf), &frame_len));
  cr_assert_eq(frame_len, 471);

  cr_assert_not(splunk_s2s_parse_v3_frame_len(buf, 3, &frame_len));
}

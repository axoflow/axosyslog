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

#include "splunk-s2s-protocol.h"

#include <string.h>

#define H1_OFF_MAGIC 0x000
#define H1_OFF_IDENTIFIER 0x080
#define H1_OFF_MGMT_PORT 0x180

void
splunk_s2s_write_varint(GString *out, guint64 value)
{
  while (TRUE)
    {
      guint8 b = value & 0x7f;
      value >>= 7;
      if (value)
        g_string_append_c(out, b | 0x80);
      else
        {
          g_string_append_c(out, b);
          return;
        }
    }
}

static void
_write_length_prefixed(GString *out, const gchar *data, gsize len)
{
  splunk_s2s_write_varint(out, len);
  g_string_append_len(out, data, len);
}

static void
_write_be_u32(GString *out, guint32 value)
{
  guint32 be = GUINT32_TO_BE(value);
  g_string_append_len(out, (const gchar *) &be, 4);
}

static void
_patch_be_u32(GString *out, gsize offset, guint32 value)
{
  guint32 be = GUINT32_TO_BE(value);
  memcpy(out->str + offset, &be, 4);
}

gboolean
splunk_s2s_format_header1(GString *out, const gchar *identifier, const gchar *mgmt_port)
{
  gsize identifier_len = strlen(identifier);
  gsize mgmt_port_len = strlen(mgmt_port);

  if (identifier_len > SPLUNK_S2S_HEADER1_MAX_IDENTIFIER_LEN || mgmt_port_len > SPLUNK_S2S_HEADER1_MAX_MGMT_PORT_LEN)
    return FALSE;

  gsize start = out->len;
  g_string_set_size(out, start + SPLUNK_S2S_HEADER1_SIZE);
  memset(out->str + start, 0, SPLUNK_S2S_HEADER1_SIZE);

  memcpy(out->str + start + H1_OFF_MAGIC, SPLUNK_S2S_MAGIC_V3, strlen(SPLUNK_S2S_MAGIC_V3));
  memcpy(out->str + start + H1_OFF_IDENTIFIER, identifier, identifier_len);
  memcpy(out->str + start + H1_OFF_MGMT_PORT, mgmt_port, mgmt_port_len);
  return TRUE;
}

/* key/value strings are transmitted with their terminating NUL included in
 * their BE-u32 length prefix; every frame carries a fixed "_raw" trailer */
void
splunk_s2s_format_v3_frame(GString *out, const SplunkS2SStringPair *pairs, gsize n_pairs)
{
  gsize len_offset = out->len;
  _write_be_u32(out, 0);

  _write_be_u32(out, n_pairs);
  for (gsize i = 0; i < n_pairs; i++)
    {
      _write_be_u32(out, strlen(pairs[i].key) + 1);
      g_string_append_len(out, pairs[i].key, strlen(pairs[i].key) + 1);
      _write_be_u32(out, strlen(pairs[i].value) + 1);
      g_string_append_len(out, pairs[i].value, strlen(pairs[i].value) + 1);
    }

  _write_be_u32(out, 0);
  _write_be_u32(out, 5);
  g_string_append_len(out, "_raw", 5);

  _patch_be_u32(out, len_offset, out->len - len_offset - 4);
}

void
splunk_s2s_format_v3_signature_frame(GString *out)
{
  SplunkS2SStringPair pairs[] =
  {
    { "__s2s_capabilities", SPLUNK_S2S_CAPABILITIES_SIGNATURE },
  };
  splunk_s2s_format_v3_frame(out, pairs, G_N_ELEMENTS(pairs));
}

/* the __s2s_capabilities pair advertising v4=1 is what switches the stream
 * to v4 framing; field set and order match a Splunk 10.2.2 forwarder */
void
splunk_s2s_format_v3_forwarder_info_frame(GString *out, const gchar *forwarder_info, const gchar *guid,
                                          guint64 timestamp)
{
  gchar timestamp_str[32];
  g_snprintf(timestamp_str, sizeof(timestamp_str), "%" G_GUINT64_FORMAT, timestamp);

  SplunkS2SStringPair pairs[] =
  {
    { "_raw", forwarder_info },
    { "_done", "_done" },
    { "_time", timestamp_str },
    { "_guid", guid },
    { "_MetaData:Index", "_internal" },
    { "__s2s_capabilities", SPLUNK_S2S_CAPABILITIES_V4 },
    { "MetaData:Sourcetype", "sourcetype::fwdinfo" },
    { "_ingLatChained", "{\"green\":{\"count\":0}}" },
    { "MetaData:Source", "source::fwd" },
    { "MetaData:Host", "host::$decideOnStartup" },
    { "_ingLatColor", "green" },
    { "__s2s_eventId", "0" },
  };
  splunk_s2s_format_v3_frame(out, pairs, G_N_ELEMENTS(pairs));
}

/* header strings are sent without their NUL, but the length prefix counts
 * it (len+1); the header list is terminated by a zero varint */
static void
_write_channel_header(GString *out, const gchar *prefix, const gchar *value)
{
  gsize prefix_len = strlen(prefix);
  gsize value_len = strlen(value);

  splunk_s2s_write_varint(out, prefix_len + value_len + 1);
  g_string_append_len(out, prefix, prefix_len);
  g_string_append_len(out, value, value_len);
}

void
splunk_s2s_format_open_channel(GString *out, guint64 channel_id, const gchar *source, const gchar *host,
                               const gchar *sourcetype)
{
  gchar channel_number[32];
  g_snprintf(channel_number, sizeof(channel_number), "%" G_GUINT64_FORMAT, channel_id);

  g_string_append_c(out, (gchar) SPLUNK_S2S_PKT_OPEN_CHANNEL);
  splunk_s2s_write_varint(out, channel_id);
  _write_channel_header(out, "source::", source);
  _write_channel_header(out, "host::", host);
  _write_channel_header(out, "sourcetype::", sourcetype);
  _write_channel_header(out, "", channel_number);
  splunk_s2s_write_varint(out, 0);
}

void
splunk_s2s_format_event(GString *out, guint64 channel_id, guint64 event_flags, guint64 timestamp,
                        const SplunkS2SEventField *fields, gsize n_fields, const gchar *raw, gsize raw_len)
{
  g_string_append_c(out, (gchar) SPLUNK_S2S_PKT_EVENT);
  splunk_s2s_write_varint(out, channel_id);
  splunk_s2s_write_varint(out, event_flags);

  if (event_flags & SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER_MASK)
    {
      splunk_s2s_write_varint(out, 0);  /* stream_id */
      splunk_s2s_write_varint(out, 0);  /* offset */
      splunk_s2s_write_varint(out, 0);  /* suboffset */
      splunk_s2s_write_varint(out, 0);  /* firstid */
      splunk_s2s_write_varint(out, timestamp);
    }
  else
    splunk_s2s_write_varint(out, 0);  /* firstid */

  splunk_s2s_write_varint(out, n_fields);
  for (gsize i = 0; i < n_fields; i++)
    {
      const SplunkS2SEventField *field = &fields[i];

      /* type byte: name_type (INLINE_STR=0) in bits 0-1, value_type in bits 2-3 */
      g_string_append_c(out, (gchar) (field->value_type << 2));
      _write_length_prefixed(out, field->name, strlen(field->name));
      if (field->value_type == SPLUNK_S2S_VALUE_NUMBER)
        splunk_s2s_write_varint(out, field->number_value);
      else
        _write_length_prefixed(out, field->str_value, field->str_value_len);
    }

  splunk_s2s_write_varint(out, raw_len);
  g_string_append_len(out, raw, raw_len);
}

void
splunk_s2s_format_close_channel(GString *out, guint64 channel_id)
{
  g_string_append_c(out, (gchar) SPLUNK_S2S_PKT_CLOSE_CHANNEL);
  splunk_s2s_write_varint(out, channel_id);
}

gboolean
splunk_s2s_parse_v3_frame_len(const guchar *buf, gsize buf_len, guint32 *frame_len)
{
  if (buf_len < 4)
    return FALSE;

  guint32 be;
  memcpy(&be, buf, 4);
  *frame_len = GUINT32_FROM_BE(be);
  return TRUE;
}

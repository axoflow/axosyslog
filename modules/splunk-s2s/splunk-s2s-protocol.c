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
splunk_s2s_format_v3_signature_frame(GString *out, const gchar *capabilities)
{
  SplunkS2SStringPair pairs[] =
  {
    { "__s2s_capabilities", capabilities },
  };
  splunk_s2s_format_v3_frame(out, pairs, G_N_ELEMENTS(pairs));
}

/* the indexer's reply to the signature frame, granting capabilities */
void
splunk_s2s_format_v3_control_frame(GString *out, const gchar *capabilities)
{
  SplunkS2SStringPair pairs[] =
  {
    { "__s2s_control_msg", capabilities },
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
                        guint64 event_id, const SplunkS2SEventField *fields, gsize n_fields,
                        const gchar *raw, gsize raw_len)
{
  g_string_append_c(out, (gchar) SPLUNK_S2S_PKT_EVENT);
  splunk_s2s_write_varint(out, channel_id);
  splunk_s2s_write_varint(out, event_flags);

  if (event_flags & SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER_MASK)
    {
      splunk_s2s_write_varint(out, 0);  /* stream_id */
      splunk_s2s_write_varint(out, 0);  /* offset */
      splunk_s2s_write_varint(out, 0);  /* suboffset */
      splunk_s2s_write_varint(out, event_id);  /* firstid */
      splunk_s2s_write_varint(out, timestamp);
    }
  else
    splunk_s2s_write_varint(out, event_id);  /* firstid */

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

/* the indexer coalesces contiguous ids into ranges and acks stragglers as
 * singles, both id forms are inclusive */
void
splunk_s2s_format_ack(GString *out, guint64 lo, guint64 hi)
{
  if (lo == hi)
    {
      g_string_append_c(out, (gchar) SPLUNK_S2S_PKT_ACK_ONE);
      splunk_s2s_write_varint(out, lo);
      return;
    }

  g_string_append_c(out, (gchar) SPLUNK_S2S_PKT_ACK_RANGE);
  splunk_s2s_write_varint(out, lo);
  splunk_s2s_write_varint(out, hi);
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

/* *pos is only advanced when a complete varint was available */
SplunkS2SParseResult
splunk_s2s_parse_varint(const guchar *buf, gsize buf_len, gsize *pos, guint64 *value)
{
  guint64 result = 0;
  gsize p = *pos;

  for (guint shift = 0; shift < 64; shift += 7)
    {
      if (p >= buf_len)
        return SPLUNK_S2S_PARSE_MORE;

      guchar b = buf[p++];

      /* the tenth byte can only contribute the topmost value bit, anything
       * above would silently wrap past 64 bits */
      if (shift == 63 && (b & 0x7e))
        return SPLUNK_S2S_PARSE_ERROR;

      result |= ((guint64) (b & 0x7f)) << shift;
      if (!(b & 0x80))
        {
          *pos = p;
          *value = result;
          return SPLUNK_S2S_PARSE_OK;
        }
    }

  return SPLUNK_S2S_PARSE_ERROR;
}

static SplunkS2SParseResult
_parse_varint(const guchar *buf, gsize buf_len, gsize *pos, guint64 *value, const gchar **error)
{
  SplunkS2SParseResult result = splunk_s2s_parse_varint(buf, buf_len, pos, value);

  if (result == SPLUNK_S2S_PARSE_ERROR)
    *error = "invalid varint running past the 64-bit ceiling";
  return result;
}

/* buf must hold a full SPLUNK_S2S_HEADER1_SIZE block; the returned strings
 * point into buf and are NUL terminated within their padded regions */
gboolean
splunk_s2s_parse_header1(const guchar *buf, const gchar **identifier, const gchar **mgmt_port)
{
  /* legacy peers never advertise v4 and stay on v3 framing for their whole
   * life, which decodes the same way */
  static const gchar *known_magics[] =
  {
    SPLUNK_S2S_MAGIC_V3,
    SPLUNK_S2S_MAGIC_V2,
    SPLUNK_S2S_MAGIC_V0,
  };

  gboolean magic_found = FALSE;
  for (gsize i = 0; i < G_N_ELEMENTS(known_magics); i++)
    {
      gsize magic_len = strlen(known_magics[i]);

      if (memcmp(buf + H1_OFF_MAGIC, known_magics[i], magic_len) == 0 && buf[H1_OFF_MAGIC + magic_len] == 0)
        {
          magic_found = TRUE;
          break;
        }
    }
  if (!magic_found)
    return FALSE;

  if (buf[H1_OFF_MGMT_PORT - 1] != 0 || buf[SPLUNK_S2S_HEADER1_SIZE - 1] != 0)
    return FALSE;

  *identifier = (const gchar *) buf + H1_OFF_IDENTIFIER;
  *mgmt_port = (const gchar *) buf + H1_OFF_MGMT_PORT;
  return TRUE;
}

SplunkS2SParseResult
splunk_s2s_parse_varint_str(const guchar *buf, gsize buf_len, gsize *pos, const gchar **data, gsize *data_len)
{
  gsize p = *pos;
  guint64 len;

  SplunkS2SParseResult result = splunk_s2s_parse_varint(buf, buf_len, &p, &len);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;
  if (len > buf_len - p)
    return SPLUNK_S2S_PARSE_MORE;

  *data = (const gchar *) buf + p;
  *data_len = len;
  *pos = p + len;
  return SPLUNK_S2S_PARSE_OK;
}

static gboolean
_parse_be_u32(const guchar *buf, gsize buf_len, gsize *pos, guint32 *value)
{
  if (buf_len - *pos < 4)
    return FALSE;

  guint32 be;
  memcpy(&be, buf + *pos, 4);
  *value = GUINT32_FROM_BE(be);
  *pos += 4;
  return TRUE;
}

/* strings are transmitted with their terminating NUL(s) counted in their
 * length prefix, hand them to the callback without the padding */
static void
_v3_string_strip_nuls(const gchar *str, gsize *len)
{
  while (*len > 0 && str[*len - 1] == '\0')
    (*len)--;
}

/* frame points at the frame body, right after the BE-u32 frame length; the
 * trailing zero u32 + "_raw" marker is padding and is not reported */
gboolean
splunk_s2s_v3_frame_foreach_pair(const guchar *frame, gsize frame_len, SplunkS2SV3PairFunc func,
                                 gpointer user_data)
{
  gsize pos = 0;
  guint32 n_pairs;

  if (!_parse_be_u32(frame, frame_len, &pos, &n_pairs))
    return FALSE;

  for (guint32 i = 0; i < n_pairs; i++)
    {
      guint32 key_len, value_len;

      if (!_parse_be_u32(frame, frame_len, &pos, &key_len) || key_len > frame_len - pos)
        return FALSE;
      const gchar *key = (const gchar *) frame + pos;
      pos += key_len;

      if (!_parse_be_u32(frame, frame_len, &pos, &value_len) || value_len > frame_len - pos)
        return FALSE;
      const gchar *value = (const gchar *) frame + pos;
      pos += value_len;

      gsize stripped_key_len = key_len, stripped_value_len = value_len;
      _v3_string_strip_nuls(key, &stripped_key_len);
      _v3_string_strip_nuls(value, &stripped_value_len);
      func(key, stripped_key_len, value, stripped_value_len, user_data);
    }

  return TRUE;
}

#define MAX_OPEN_CHANNEL_COLUMNS 4096
#define MAX_FIELD_NAME_LEN 1024

/* header strings count a phantom NUL in their length prefix that is not on
 * the wire; a zero length prefix marks an absent slot */
static SplunkS2SParseResult
_parse_channel_header(const guchar *buf, gsize buf_len, gsize *pos, SplunkS2SParsedChannel *channel,
                      guint64 header_len)
{
  if (header_len - 1 > buf_len - *pos)
    return SPLUNK_S2S_PARSE_MORE;

  if (channel->n_headers < SPLUNK_S2S_MAX_CHANNEL_HEADERS)
    {
      channel->headers[channel->n_headers] = (const gchar *) buf + *pos;
      channel->header_lens[channel->n_headers] = header_len - 1;
      channel->n_headers++;
    }
  *pos += header_len - 1;
  return SPLUNK_S2S_PARSE_OK;
}

/* the channel's indexed-extraction column table: a count and that many plain
 * length-prefixed names; ordinary channels send a zero count */
static SplunkS2SParseResult
_parse_channel_columns(const guchar *buf, gsize buf_len, gsize *pos, const gchar **error)
{
  guint64 n_columns;

  SplunkS2SParseResult result = _parse_varint(buf, buf_len, pos, &n_columns, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;
  if (n_columns > MAX_OPEN_CHANNEL_COLUMNS)
    {
      *error = "implausible column count in the channel header, the stream is probably desynchronized";
      return SPLUNK_S2S_PARSE_ERROR;
    }

  for (guint64 i = 0; i < n_columns; i++)
    {
      guint64 column_len;

      result = _parse_varint(buf, buf_len, pos, &column_len, error);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
      if (column_len > MAX_FIELD_NAME_LEN)
        {
          *error = "implausible column name length in the channel header, the stream is probably desynchronized";
          return SPLUNK_S2S_PARSE_ERROR;
        }
      if (column_len > buf_len - *pos)
        return SPLUNK_S2S_PARSE_MORE;
      *pos += column_len;
    }

  return SPLUNK_S2S_PARSE_OK;
}

/* exactly four fixed metadata slots (source::, host::, sourcetype:: and a
 * "cd" slot, any of which may be absent), then the column table */
static SplunkS2SParseResult
_parse_channel_slots_and_columns(const guchar *buf, gsize buf_len, gsize *pos, SplunkS2SParsedChannel *channel,
                                 const gchar **error)
{
  for (gint i = 0; i < 4; i++)
    {
      guint64 header_len;

      SplunkS2SParseResult result = _parse_varint(buf, buf_len, pos, &header_len, error);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
      if (header_len == 0)
        continue;

      result = _parse_channel_header(buf, buf_len, pos, channel, header_len);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
    }

  return _parse_channel_columns(buf, buf_len, pos, error);
}

SplunkS2SParseResult
splunk_s2s_parse_open_channel(const guchar *buf, gsize buf_len, gsize *pos, SplunkS2SParsedChannel *channel,
                              const gchar **error)
{
  gsize p = *pos;

  memset(channel, 0, sizeof(*channel));
  SplunkS2SParseResult result = _parse_varint(buf, buf_len, &p, &channel->channel_id, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;

  result = _parse_channel_slots_and_columns(buf, buf_len, &p, channel, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;

  *pos = p;
  return SPLUNK_S2S_PARSE_OK;
}

/* like OPEN_CHANNEL, but the new channel id is PRECEDED by the template
 * channel to inherit from: events always follow the second varint and the
 * slots are followed by the same column count */
SplunkS2SParseResult
splunk_s2s_parse_clone_channel(const guchar *buf, gsize buf_len, gsize *pos, SplunkS2SParsedChannel *channel,
                               const gchar **error)
{
  gsize p = *pos;

  memset(channel, 0, sizeof(*channel));
  SplunkS2SParseResult result = _parse_varint(buf, buf_len, &p, &channel->template_channel_id, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;
  result = _parse_varint(buf, buf_len, &p, &channel->channel_id, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;

  result = _parse_channel_slots_and_columns(buf, buf_len, &p, channel, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;

  *pos = p;
  return SPLUNK_S2S_PARSE_OK;
}

/* MetaData:Source/Host/Sourcetype values carry a "source::" style prefix,
 * _MetaData:Index is the bare index name */
void
splunk_s2s_strip_meta_prefix(const gchar **value, gsize *len)
{
  for (gsize i = 0; i + 1 < *len; i++)
    {
      if ((*value)[i] == ':' && (*value)[i + 1] == ':')
        {
          *value += i + 2;
          *len -= i + 2;
          return;
        }
    }
}

static gboolean
_field_name_equals(const gchar *name, gsize name_len, const gchar *expected)
{
  gsize expected_len = strlen(expected);

  return name_len == expected_len && memcmp(name, expected, expected_len) == 0;
}

static void
_collect_metadata_field(SplunkS2SParsedEvent *event, const gchar *name, gsize name_len,
                        const gchar *value, gsize value_len)
{
  if (_field_name_equals(name, name_len, "_MetaData:Index"))
    {
      event->index = value;
      event->index_len = value_len;
      return;
    }

  if (_field_name_equals(name, name_len, "MetaData:Source"))
    {
      splunk_s2s_strip_meta_prefix(&value, &value_len);
      event->source = value;
      event->source_len = value_len;
      return;
    }

  if (_field_name_equals(name, name_len, "MetaData:Sourcetype"))
    {
      splunk_s2s_strip_meta_prefix(&value, &value_len);
      event->sourcetype = value;
      event->sourcetype_len = value_len;
      return;
    }

  if (_field_name_equals(name, name_len, "MetaData:Host"))
    {
      splunk_s2s_strip_meta_prefix(&value, &value_len);
      event->host = value;
      event->host_len = value_len;
    }
}

static SplunkS2SParseResult
_parse_event_field(const guchar *buf, gsize buf_len, gsize *pos, SplunkS2SParsedEvent *event,
                   const gchar **error)
{
  guint64 type_desc, discard;
  SplunkS2SParseResult result;

  result = _parse_varint(buf, buf_len, pos, &type_desc, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;

  guint64 name_type = type_desc & 0x3;
  guint64 value_type = (type_desc >> 2) & 0x3;
  guint64 flag_bits = type_desc >> 4;

  const gchar *name = NULL;
  gsize name_len = 0;
  if (name_type == SPLUNK_S2S_NAME_INLINE_STR || name_type == SPLUNK_S2S_NAME_META_INLINE_STR)
    {
      guint64 len;

      result = _parse_varint(buf, buf_len, pos, &len, error);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
      if (len > MAX_FIELD_NAME_LEN)
        {
          *error = "implausible field name length, the stream is probably desynchronized";
          return SPLUNK_S2S_PARSE_ERROR;
        }
      if (len > buf_len - *pos)
        return SPLUNK_S2S_PARSE_MORE;
      name = (const gchar *) buf + *pos;
      name_len = len;
      *pos += len;
    }
  else
    {
      /* a predefined or dynamic name table code */
      result = _parse_varint(buf, buf_len, pos, &discard, error);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
    }

  const gchar *value = NULL;
  gsize value_len = 0;
  switch (value_type)
    {
    case SPLUNK_S2S_VALUE_NUMBER:
      result = _parse_varint(buf, buf_len, pos, &discard, error);
      break;
    case SPLUNK_S2S_VALUE_STR:
      result = splunk_s2s_parse_varint_str(buf, buf_len, pos, &value, &value_len);
      if (result == SPLUNK_S2S_PARSE_ERROR)
        *error = "invalid varint running past the 64-bit ceiling";
      break;
    case SPLUNK_S2S_VALUE_CODE_PREDEFINED:
      /* with the offset/length flag the value is a payload reference
       * instead of a table code */
      result = _parse_varint(buf, buf_len, pos, &discard, error);
      if (result == SPLUNK_S2S_PARSE_OK && (flag_bits & SPLUNK_S2S_FIELD_FLAG_CODE_IS_OFFSET_LEN))
        result = _parse_varint(buf, buf_len, pos, &discard, error);
      break;
    case SPLUNK_S2S_VALUE_RAW_OFFSET_LEN:
      result = _parse_varint(buf, buf_len, pos, &discard, error);
      if (result == SPLUNK_S2S_PARSE_OK)
        result = _parse_varint(buf, buf_len, pos, &discard, error);
      break;
    default:
      g_assert_not_reached();
    }
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;

  if (flag_bits & SPLUNK_S2S_FIELD_FLAG_HAS_EXTRAS)
    {
      result = _parse_varint(buf, buf_len, pos, &discard, error);
      if (result == SPLUNK_S2S_PARSE_OK)
        result = _parse_varint(buf, buf_len, pos, &discard, error);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
    }

  if (name && value)
    _collect_metadata_field(event, name, name_len, value, value_len);

  return SPLUNK_S2S_PARSE_OK;
}

SplunkS2SParseResult
splunk_s2s_parse_event(const guchar *buf, gsize buf_len, gsize *pos, SplunkS2SParsedEvent *event,
                       const gchar **error)
{
  gsize p = *pos;
  guint64 discard;
  SplunkS2SParseResult result;

  memset(event, 0, sizeof(*event));
  result = _parse_varint(buf, buf_len, &p, &event->channel_id, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;
  result = _parse_varint(buf, buf_len, &p, &event->flags, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;

  if (event->flags & SPLUNK_S2S_EVENT_FLAG_STREAM_POS)
    {
      /* stream id, offset, suboffset */
      for (gint i = 0; i < 3; i++)
        {
          result = _parse_varint(buf, buf_len, &p, &discard, error);
          if (result != SPLUNK_S2S_PARSE_OK)
            return result;
        }
    }
  if (event->flags & SPLUNK_S2S_EVENT_FLAG_EVENT_ID)
    {
      result = _parse_varint(buf, buf_len, &p, &event->event_id, error);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
    }
  if (event->flags & SPLUNK_S2S_EVENT_FLAG_TIMESTAMP)
    {
      result = _parse_varint(buf, buf_len, &p, &event->timestamp, error);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
    }

  guint64 n_fields;
  result = _parse_varint(buf, buf_len, &p, &n_fields, error);
  if (result != SPLUNK_S2S_PARSE_OK)
    return result;
  for (guint64 i = 0; i < n_fields; i++)
    {
      result = _parse_event_field(buf, buf_len, &p, event, error);
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
    }

  /* metric events carry a binary sample section before the payload */
  const gchar *raw_section;
  gsize raw_section_len;
  if (event->flags & SPLUNK_S2S_EVENT_FLAG_RAW_SECTION)
    {
      result = splunk_s2s_parse_varint_str(buf, buf_len, &p, &raw_section, &raw_section_len);
      if (result != SPLUNK_S2S_PARSE_OK)
        goto varint_str_result;
    }

  result = splunk_s2s_parse_varint_str(buf, buf_len, &p, &event->raw, &event->raw_len);
  if (result != SPLUNK_S2S_PARSE_OK)
    goto varint_str_result;

  *pos = p;
  return SPLUNK_S2S_PARSE_OK;

varint_str_result:
  if (result == SPLUNK_S2S_PARSE_ERROR)
    *error = "invalid varint running past the 64-bit ceiling";
  return result;
}

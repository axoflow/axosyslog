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

#include "splunk-s2s-proto-server.h"
#include "splunk-s2s-protocol.h"

#include "messages.h"

#include <errno.h>
#include <string.h>

#define MAX_V3_FRAME_LEN (1024 * 1024)
#define READ_CHUNK_SIZE 8192
#define MAX_READS_PER_POLL 100

/* a v4 packet wraps the event payload plus the packet header, the field
 * dictionary and an optional metric sample section; heavy forwarders
 * emit merged events far larger than the default log-msg-size(), so the
 * receive buffer has a generous floor to let the payload reach the
 * message-level size check (where it can be trimmed) instead of tearing
 * down the connection mid-packet */
#define MAX_PACKET_OVERHEAD (64 * 1024)
#define RECEIVE_BUFFER_FLOOR (8 * 1024 * 1024)

/*
 * When the forwarder requested acknowledgements (useACK), decoded event ids
 * are confirmed on the reverse channel, coalesced into ranges; without
 * this, such forwarders stop sending after their first batch.
 */

typedef enum
{
  SS2S_SRV_PROTO_HEADER1,
  SS2S_SRV_PROTO_V3,
  SS2S_SRV_PROTO_V4,
} LogProtoSplunkS2SServerPhase;

typedef struct _SplunkS2SServerChannel
{
  /* metadata defaults from the channel header */
  gchar *source;
  gchar *host;
  gchar *sourcetype;

  /* reassembly of raw chunks and the metadata their lines inherit */
  GString *chunk_buf;
  gchar *chunk_index;
  gchar *chunk_source;
  gchar *chunk_sourcetype;
  gchar *chunk_host;
  guint64 chunk_timestamp;
} SplunkS2SServerChannel;

typedef struct _SplunkS2SServerMessage
{
  GString *raw;
  gchar *index;
  gchar *source;
  gchar *sourcetype;
  gchar *host;
  guint64 timestamp;
} SplunkS2SServerMessage;

typedef struct _SplunkS2SEventMeta
{
  const gchar *index;
  gsize index_len;
  const gchar *source;
  gsize source_len;
  const gchar *sourcetype;
  gsize sourcetype_len;
  const gchar *host;
  gsize host_len;
  guint64 timestamp;
} SplunkS2SEventMeta;

typedef struct _LogProtoSplunkS2SServer
{
  LogProtoServer super;
  LogProtoSplunkS2SServerPhase phase;
  gboolean broken;
  gboolean eof_seen;
  gboolean tls_checked;

  GString *in_buf;
  gsize in_pos;

  GString *out_buf;
  gsize out_pos;

  gboolean signature_seen;
  gboolean ack_requested;

  GHashTable *channels;
  GQueue pending_messages;
  SplunkS2SServerMessage *current_message;

  /* run of contiguous event ids waiting to be acknowledged */
  gboolean ack_run_active;
  guint64 ack_lo;
  guint64 ack_hi;
} LogProtoSplunkS2SServer;

static void
_message_free(SplunkS2SServerMessage *message)
{
  g_string_free(message->raw, TRUE);
  g_free(message->index);
  g_free(message->source);
  g_free(message->sourcetype);
  g_free(message->host);
  g_free(message);
}

static void
_channel_free(gpointer data)
{
  SplunkS2SServerChannel *channel = data;

  g_free(channel->source);
  g_free(channel->host);
  g_free(channel->sourcetype);
  g_string_free(channel->chunk_buf, TRUE);
  g_free(channel->chunk_index);
  g_free(channel->chunk_source);
  g_free(channel->chunk_sourcetype);
  g_free(channel->chunk_host);
  g_free(channel);
}

static LogProtoStatus
_broken(LogProtoSplunkS2SServer *self, LogProtoStatus status)
{
  self->broken = TRUE;
  return status;
}

static gboolean
_out_buf_pending(LogProtoSplunkS2SServer *self)
{
  return self->out_pos < self->out_buf->len;
}

static LogProtoStatus
_flush_out_buf(LogProtoSplunkS2SServer *self)
{
  if (!_out_buf_pending(self))
    return LPS_SUCCESS;

  gssize rc = log_transport_stack_write(&self->super.transport_stack,
                                        self->out_buf->str + self->out_pos,
                                        self->out_buf->len - self->out_pos);
  if (rc < 0)
    {
      if (errno == EAGAIN || errno == EINTR)
        return LPS_PARTIAL;

      msg_error("splunk-s2s: I/O error occurred while writing",
                evt_tag_int("fd", self->super.transport_stack.fd),
                evt_tag_error(EVT_TAG_OSERROR));
      return LPS_ERROR;
    }

  self->out_pos += rc;
  if (_out_buf_pending(self))
    return LPS_PARTIAL;

  g_string_truncate(self->out_buf, 0);
  self->out_pos = 0;
  return LPS_SUCCESS;
}

static gboolean
_queue_message(LogProtoSplunkS2SServer *self, const gchar *raw, gsize raw_len, const SplunkS2SEventMeta *meta)
{
  if (raw_len > (gsize) self->super.options->max_msg_size)
    {
      if (!self->super.options->trim_large_messages)
        {
          msg_error("splunk-s2s: incoming event larger than log-msg-size()",
                    evt_tag_int("fd", self->super.transport_stack.fd),
                    evt_tag_long("event_size", raw_len),
                    evt_tag_int("log_msg_size", self->super.options->max_msg_size));
          return FALSE;
        }
      raw_len = self->super.options->max_msg_size;
    }

  SplunkS2SServerMessage *message = g_new0(SplunkS2SServerMessage, 1);
  message->raw = g_string_new_len(raw, raw_len);
  if (meta->index_len)
    message->index = g_strndup(meta->index, meta->index_len);
  if (meta->source_len)
    message->source = g_strndup(meta->source, meta->source_len);
  if (meta->sourcetype_len)
    message->sourcetype = g_strndup(meta->sourcetype, meta->sourcetype_len);
  if (meta->host_len)
    message->host = g_strndup(meta->host, meta->host_len);
  message->timestamp = meta->timestamp;

  g_queue_push_tail(&self->pending_messages, message);
  return TRUE;
}

/* coalesce contiguous ids like an indexer does */
static void
_emit_ack_run(LogProtoSplunkS2SServer *self)
{
  if (!self->ack_run_active)
    return;

  splunk_s2s_format_ack(self->out_buf, self->ack_lo, self->ack_hi);
  self->ack_run_active = FALSE;
}

static void
_register_ack(LogProtoSplunkS2SServer *self, guint64 event_id)
{
  if (self->ack_run_active && event_id == self->ack_hi + 1)
    {
      self->ack_hi = event_id;
      return;
    }

  _emit_ack_run(self);
  self->ack_run_active = TRUE;
  self->ack_lo = event_id;
  self->ack_hi = event_id;
}

static gboolean
_capability_enabled(const gchar *capabilities, gsize capabilities_len, const gchar *key)
{
  gsize key_len = strlen(key);
  gsize i = 0;

  while (i < capabilities_len)
    {
      gsize start = i;
      while (i < capabilities_len && capabilities[i] != ';')
        i++;

      if (i - start > key_len + 1
          && memcmp(capabilities + start, key, key_len) == 0
          && capabilities[start + key_len] == '=')
        {
          gchar value = capabilities[start + key_len + 1];
          return value == '1' || value == 't';
        }

      if (i < capabilities_len)
        i++;
    }
  return FALSE;
}

static guint64
_parse_decimal(const gchar *str, gsize len)
{
  guint64 value = 0;

  for (gsize i = 0; i < len; i++)
    {
      if (str[i] < '0' || str[i] > '9')
        return 0;
      value = value * 10 + (str[i] - '0');
    }
  return value;
}

typedef struct _SplunkS2SV3FrameInfo
{
  const gchar *capabilities;
  gsize capabilities_len;
  const gchar *raw;
  gsize raw_len;
  SplunkS2SEventMeta meta;
} SplunkS2SV3FrameInfo;

static gboolean
_key_equals(const gchar *key, gsize key_len, const gchar *expected)
{
  gsize expected_len = strlen(expected);

  return key_len == expected_len && memcmp(key, expected, expected_len) == 0;
}

static void
_collect_v3_pair(const gchar *key, gsize key_len, const gchar *value, gsize value_len, gpointer user_data)
{
  SplunkS2SV3FrameInfo *info = user_data;

  if (_key_equals(key, key_len, "__s2s_capabilities"))
    {
      info->capabilities = value;
      info->capabilities_len = value_len;
    }
  else if (_key_equals(key, key_len, "_raw"))
    {
      info->raw = value;
      info->raw_len = value_len;
    }
  else if (_key_equals(key, key_len, "_time"))
    info->meta.timestamp = _parse_decimal(value, value_len);
  else if (_key_equals(key, key_len, "_MetaData:Index"))
    {
      info->meta.index = value;
      info->meta.index_len = value_len;
    }
  else if (_key_equals(key, key_len, "MetaData:Source"))
    {
      splunk_s2s_strip_meta_prefix(&value, &value_len);
      info->meta.source = value;
      info->meta.source_len = value_len;
    }
  else if (_key_equals(key, key_len, "MetaData:Sourcetype"))
    {
      splunk_s2s_strip_meta_prefix(&value, &value_len);
      info->meta.sourcetype = value;
      info->meta.sourcetype_len = value_len;
    }
  else if (_key_equals(key, key_len, "MetaData:Host"))
    {
      splunk_s2s_strip_meta_prefix(&value, &value_len);
      info->meta.host = value;
      info->meta.host_len = value_len;
    }
}

static void
_queue_hello(LogProtoSplunkS2SServer *self)
{
  splunk_s2s_format_v3_control_frame(self->out_buf, SPLUNK_S2S_CAPABILITIES_SERVER_HELLO);
}

static gboolean
_process_v3_frame(LogProtoSplunkS2SServer *self, const guchar *frame, gsize frame_len)
{
  SplunkS2SV3FrameInfo info = {0};

  if (!splunk_s2s_v3_frame_foreach_pair(frame, frame_len, _collect_v3_pair, &info))
    {
      msg_error("splunk-s2s: malformed v3 frame received",
                evt_tag_int("fd", self->super.transport_stack.fd));
      return FALSE;
    }

  /* the first frame is the forwarder's signature carrying its capability
   * requests, the reply grants v4 */
  if (!self->signature_seen)
    {
      self->signature_seen = TRUE;
      if (info.capabilities && _capability_enabled(info.capabilities, info.capabilities_len, "compression"))
        {
          msg_error("splunk-s2s: the forwarder requested stream compression, which is not supported, "
                    "set compressed=false in its outputs.conf",
                    evt_tag_int("fd", self->super.transport_stack.fd));
          return FALSE;
        }
      self->ack_requested = info.capabilities
                            && _capability_enabled(info.capabilities, info.capabilities_len, "ack");
      _queue_hello(self);
    }

  if (info.capabilities && _capability_enabled(info.capabilities, info.capabilities_len, "v4"))
    self->phase = SS2S_SRV_PROTO_V4;

  if (info.raw_len)
    return _queue_message(self, info.raw, info.raw_len, &info.meta);
  return TRUE;
}

static SplunkS2SParseResult
_decode_header1(LogProtoSplunkS2SServer *self)
{
  if (self->in_buf->len - self->in_pos < SPLUNK_S2S_HEADER1_SIZE)
    return SPLUNK_S2S_PARSE_MORE;

  const gchar *identifier, *mgmt_port;
  if (!splunk_s2s_parse_header1((const guchar *) self->in_buf->str + self->in_pos, &identifier, &mgmt_port))
    {
      if (memcmp(self->in_buf->str + self->in_pos, SPLUNK_S2S_MAGIC_V2_COMPRESSED,
                 strlen(SPLUNK_S2S_MAGIC_V2_COMPRESSED)) == 0)
        msg_error("splunk-s2s: the forwarder speaks the legacy compressed protocol, which is not supported, "
                  "set compressed=false in its outputs.conf",
                  evt_tag_int("fd", self->super.transport_stack.fd));
      else
        msg_error("splunk-s2s: invalid greeting, the peer is probably not a Splunk forwarder",
                  evt_tag_int("fd", self->super.transport_stack.fd));
      return SPLUNK_S2S_PARSE_ERROR;
    }

  msg_debug("splunk-s2s: forwarder connected",
            evt_tag_str("identifier", identifier),
            evt_tag_str("mgmt_port", mgmt_port),
            evt_tag_int("fd", self->super.transport_stack.fd));

  self->in_pos += SPLUNK_S2S_HEADER1_SIZE;
  self->phase = SS2S_SRV_PROTO_V3;
  return SPLUNK_S2S_PARSE_OK;
}

static SplunkS2SParseResult
_decode_v3(LogProtoSplunkS2SServer *self)
{
  const guchar *buf = (const guchar *) self->in_buf->str;
  gsize len = self->in_buf->len;
  guint32 frame_len;

  if (len - self->in_pos < 4 || !splunk_s2s_parse_v3_frame_len(buf + self->in_pos, len - self->in_pos, &frame_len))
    return SPLUNK_S2S_PARSE_MORE;

  if (frame_len > MAX_V3_FRAME_LEN)
    {
      msg_error("splunk-s2s: unexpected v3 frame length, the peer is probably not a Splunk forwarder",
                evt_tag_int("fd", self->super.transport_stack.fd),
                evt_tag_long("frame_len", frame_len));
      return SPLUNK_S2S_PARSE_ERROR;
    }

  /* on a compressed connection the handshake frames after the plaintext
   * greeting are already zlib blocks: the first payload byte of a plaintext v3
   * frame is the always-zero MSB of the pair count, a zlib CMF/FLG header
   * never is */
  if (len - self->in_pos >= 6)
    {
      guchar cmf = buf[self->in_pos + 4];
      guchar flg = buf[self->in_pos + 5];

      if ((cmf & 0x0f) == 8 && ((cmf << 8) | flg) % 31 == 0)
        {
          msg_error("splunk-s2s: the forwarder compresses the stream, which is not supported, "
                    "set compressed=false in its outputs.conf",
                    evt_tag_int("fd", self->super.transport_stack.fd));
          return SPLUNK_S2S_PARSE_ERROR;
        }
    }

  if (len - self->in_pos < 4 + (gsize) frame_len)
    return SPLUNK_S2S_PARSE_MORE;

  if (!_process_v3_frame(self, buf + self->in_pos + 4, frame_len))
    return SPLUNK_S2S_PARSE_ERROR;

  self->in_pos += 4 + frame_len;
  return SPLUNK_S2S_PARSE_OK;
}

static SplunkS2SServerChannel *
_lookup_channel(LogProtoSplunkS2SServer *self, guint64 channel_id)
{
  return g_hash_table_lookup(self->channels, &channel_id);
}

static gboolean
_flush_chunk_remainder(LogProtoSplunkS2SServer *self, SplunkS2SServerChannel *channel);

static void
_set_channel(LogProtoSplunkS2SServer *self, const SplunkS2SParsedChannel *parsed)
{
  SplunkS2SServerChannel *previous = _lookup_channel(self, parsed->channel_id);
  if (previous)
    _flush_chunk_remainder(self, previous);

  SplunkS2SServerChannel *channel = g_new0(SplunkS2SServerChannel, 1);
  channel->chunk_buf = g_string_sized_new(0);

  for (gsize i = 0; i < parsed->n_headers; i++)
    {
      const gchar *header = parsed->headers[i];
      const gchar *value = header;
      gsize value_len = parsed->header_lens[i];

      splunk_s2s_strip_meta_prefix(&value, &value_len);
      if (value == header)
        continue;

      gsize prefix_len = value - header;
      if (prefix_len == strlen("source::") && memcmp(header, "source::", prefix_len) == 0)
        {
          g_free(channel->source);
          channel->source = g_strndup(value, value_len);
        }
      else if (prefix_len == strlen("host::") && memcmp(header, "host::", prefix_len) == 0)
        {
          g_free(channel->host);
          channel->host = g_strndup(value, value_len);
        }
      else if (prefix_len == strlen("sourcetype::") && memcmp(header, "sourcetype::", prefix_len) == 0)
        {
          g_free(channel->sourcetype);
          channel->sourcetype = g_strndup(value, value_len);
        }
    }

  guint64 *key = g_new(guint64, 1);
  *key = parsed->channel_id;
  g_hash_table_replace(self->channels, key, channel);
}

static void
_resolve_event_meta(const SplunkS2SServerChannel *channel, const SplunkS2SParsedEvent *event,
                    SplunkS2SEventMeta *meta)
{
  meta->index = event->index;
  meta->index_len = event->index_len;

  meta->source = event->source;
  meta->source_len = event->source_len;
  if (!meta->source_len && channel->source)
    {
      meta->source = channel->source;
      meta->source_len = strlen(channel->source);
    }

  meta->sourcetype = event->sourcetype;
  meta->sourcetype_len = event->sourcetype_len;
  if (!meta->sourcetype_len && channel->sourcetype)
    {
      meta->sourcetype = channel->sourcetype;
      meta->sourcetype_len = strlen(channel->sourcetype);
    }

  meta->host = event->host;
  meta->host_len = event->host_len;
  if (!meta->host_len && channel->host)
    {
      meta->host = channel->host;
      meta->host_len = strlen(channel->host);
    }

  meta->timestamp = event->timestamp;
}

static void
_chunk_meta(const SplunkS2SServerChannel *channel, SplunkS2SEventMeta *meta)
{
  meta->index = channel->chunk_index;
  meta->index_len = channel->chunk_index ? strlen(channel->chunk_index) : 0;
  meta->source = channel->chunk_source;
  meta->source_len = channel->chunk_source ? strlen(channel->chunk_source) : 0;
  meta->sourcetype = channel->chunk_sourcetype;
  meta->sourcetype_len = channel->chunk_sourcetype ? strlen(channel->chunk_sourcetype) : 0;
  meta->host = channel->chunk_host;
  meta->host_len = channel->chunk_host ? strlen(channel->chunk_host) : 0;
  meta->timestamp = channel->chunk_timestamp;
}

/* split the reassembled chunk on line breaks the way an indexer's line
 * breaker would */
static gboolean
_split_chunk_lines(LogProtoSplunkS2SServer *self, SplunkS2SServerChannel *channel)
{
  GString *chunk = channel->chunk_buf;
  SplunkS2SEventMeta meta;
  gsize start = 0;

  _chunk_meta(channel, &meta);

  while (start < chunk->len)
    {
      const gchar *line_end = memchr(chunk->str + start, '\n', chunk->len - start);
      if (!line_end)
        break;

      gsize line_len = line_end - (chunk->str + start);
      if (line_len && chunk->str[start + line_len - 1] == '\r')
        line_len--;

      if (line_len && !_queue_message(self, chunk->str + start, line_len, &meta))
        return FALSE;

      start = (line_end - chunk->str) + 1;
    }

  if (start)
    g_string_erase(chunk, 0, start);
  return TRUE;
}

static gboolean
_flush_chunk_remainder(LogProtoSplunkS2SServer *self, SplunkS2SServerChannel *channel)
{
  if (!channel->chunk_buf->len)
    return TRUE;

  SplunkS2SEventMeta meta;
  _chunk_meta(channel, &meta);

  gboolean result = _queue_message(self, channel->chunk_buf->str, channel->chunk_buf->len, &meta);
  g_string_truncate(channel->chunk_buf, 0);
  return result;
}

static void
_update_string(gchar **stored, const gchar *value, gsize value_len)
{
  g_free(*stored);
  *stored = value_len ? g_strndup(value, value_len) : NULL;
}

static gboolean
_process_chunk(LogProtoSplunkS2SServer *self, SplunkS2SServerChannel *channel,
               const SplunkS2SParsedEvent *event, const SplunkS2SEventMeta *meta)
{
  _update_string(&channel->chunk_index, meta->index, meta->index_len);
  _update_string(&channel->chunk_source, meta->source, meta->source_len);
  _update_string(&channel->chunk_sourcetype, meta->sourcetype, meta->sourcetype_len);
  _update_string(&channel->chunk_host, meta->host, meta->host_len);
  channel->chunk_timestamp = meta->timestamp;

  g_string_append_len(channel->chunk_buf, event->raw, event->raw_len);
  return _split_chunk_lines(self, channel);
}

static gboolean
_process_v4_event(LogProtoSplunkS2SServer *self, const SplunkS2SParsedEvent *event)
{
  SplunkS2SServerChannel *channel = _lookup_channel(self, event->channel_id);
  if (!channel)
    {
      msg_error("splunk-s2s: event received on a channel that was never opened",
                evt_tag_int("fd", self->super.transport_stack.fd),
                evt_tag_long("channel_id", event->channel_id));
      return FALSE;
    }

  if (self->ack_requested && (event->flags & SPLUNK_S2S_EVENT_FLAG_EVENT_ID))
    _register_ack(self, event->event_id);

  /* empty payloads are batch markers */
  if (!event->raw_len)
    return TRUE;

  SplunkS2SEventMeta meta;
  _resolve_event_meta(channel, event, &meta);

  if (event->flags & SPLUNK_S2S_EVENT_FLAG_AGGREGATED)
    return _queue_message(self, event->raw, event->raw_len, &meta);

  return _process_chunk(self, channel, event, &meta);
}

static SplunkS2SParseResult
_decode_v4(LogProtoSplunkS2SServer *self)
{
  const guchar *buf = (const guchar *) self->in_buf->str;
  gsize len = self->in_buf->len;
  gsize pos = self->in_pos;
  const gchar *error = NULL;
  SplunkS2SParseResult result;

  if (pos >= len)
    return SPLUNK_S2S_PARSE_MORE;

  guchar tag = buf[pos++];
  switch (tag)
    {
    case SPLUNK_S2S_PKT_INIT:
    {
      const gchar *payload;
      gsize payload_len;

      result = splunk_s2s_parse_varint_str(buf, len, &pos, &payload, &payload_len);
      if (result == SPLUNK_S2S_PARSE_ERROR)
        {
          error = "invalid varint running past the 64-bit ceiling";
          goto parse_error;
        }
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
      msg_debug("splunk-s2s: forwarder info received",
                evt_tag_printf("info", "%.*s", (gint) MIN(payload_len, 256), payload),
                evt_tag_int("fd", self->super.transport_stack.fd));
      break;
    }
    case SPLUNK_S2S_PKT_OPEN_CHANNEL:
    case SPLUNK_S2S_PKT_CLONE_CHANNEL:
    {
      SplunkS2SParsedChannel parsed;

      if (tag == SPLUNK_S2S_PKT_OPEN_CHANNEL)
        result = splunk_s2s_parse_open_channel(buf, len, &pos, &parsed, &error);
      else
        result = splunk_s2s_parse_clone_channel(buf, len, &pos, &parsed, &error);
      if (result == SPLUNK_S2S_PARSE_ERROR)
        goto parse_error;
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
      _set_channel(self, &parsed);
      break;
    }
    case SPLUNK_S2S_PKT_CLOSE_CHANNEL:
    {
      guint64 channel_id;

      result = splunk_s2s_parse_varint(buf, len, &pos, &channel_id);
      if (result == SPLUNK_S2S_PARSE_ERROR)
        {
          error = "invalid varint running past the 64-bit ceiling";
          goto parse_error;
        }
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;

      SplunkS2SServerChannel *channel = _lookup_channel(self, channel_id);
      if (channel)
        {
          if (!_flush_chunk_remainder(self, channel))
            return SPLUNK_S2S_PARSE_ERROR;
          g_hash_table_remove(self->channels, &channel_id);
        }
      break;
    }
    case SPLUNK_S2S_PKT_EVENT:
    {
      SplunkS2SParsedEvent event;

      result = splunk_s2s_parse_event(buf, len, &pos, &event, &error);
      if (result == SPLUNK_S2S_PARSE_ERROR)
        goto parse_error;
      if (result != SPLUNK_S2S_PARSE_OK)
        return result;
      if (!_process_v4_event(self, &event))
        return SPLUNK_S2S_PARSE_ERROR;
      break;
    }
    case SPLUNK_S2S_PKT_START_ZLIB:
      msg_error("splunk-s2s: the forwarder switched to stream compression, which is not supported, "
                "set compressed=false in its outputs.conf",
                evt_tag_int("fd", self->super.transport_stack.fd));
      return SPLUNK_S2S_PARSE_ERROR;
    default:
      msg_error("splunk-s2s: unexpected packet, the stream is desynchronized or the peer is not a Splunk forwarder",
                evt_tag_printf("tag", "0x%02x", tag),
                evt_tag_int("fd", self->super.transport_stack.fd));
      return SPLUNK_S2S_PARSE_ERROR;
    }

  self->in_pos = pos;
  return SPLUNK_S2S_PARSE_OK;

parse_error:
  msg_error("splunk-s2s: failed to parse packet",
            evt_tag_printf("tag", "0x%02x", tag),
            evt_tag_str("reason", error),
            evt_tag_int("fd", self->super.transport_stack.fd));
  return SPLUNK_S2S_PARSE_ERROR;
}

static LogProtoStatus
_decode_available(LogProtoSplunkS2SServer *self)
{
  while (TRUE)
    {
      SplunkS2SParseResult result;

      switch (self->phase)
        {
        case SS2S_SRV_PROTO_HEADER1:
          result = _decode_header1(self);
          break;
        case SS2S_SRV_PROTO_V3:
          result = _decode_v3(self);
          break;
        case SS2S_SRV_PROTO_V4:
          result = _decode_v4(self);
          break;
        default:
          g_assert_not_reached();
        }

      if (result == SPLUNK_S2S_PARSE_ERROR)
        return LPS_ERROR;
      if (result == SPLUNK_S2S_PARSE_MORE)
        break;
    }

  gsize receive_buffer_limit = MAX((gsize) self->super.options->max_buffer_size + MAX_PACKET_OVERHEAD,
                                   (gsize) RECEIVE_BUFFER_FLOOR);
  if (self->in_buf->len - self->in_pos > receive_buffer_limit)
    {
      msg_error("splunk-s2s: incoming packet larger than the receive buffer, increase log-msg-size()",
                evt_tag_int("fd", self->super.transport_stack.fd),
                evt_tag_long("buffered_bytes", self->in_buf->len - self->in_pos),
                evt_tag_long("receive_buffer_limit", receive_buffer_limit));
      return LPS_ERROR;
    }

  if (self->in_pos > 0)
    {
      g_string_erase(self->in_buf, 0, self->in_pos);
      self->in_pos = 0;
    }

  _emit_ack_run(self);
  return LPS_SUCCESS;
}

static gint
_compare_channel_ids(gconstpointer a, gconstpointer b)
{
  guint64 id_a = *(const guint64 *) a;
  guint64 id_b = *(const guint64 *) b;

  return id_a < id_b ? -1 : id_a != id_b;
}

static gboolean
_flush_all_chunk_remainders(LogProtoSplunkS2SServer *self)
{
  GList *channel_ids = g_list_sort(g_hash_table_get_keys(self->channels), _compare_channel_ids);
  gboolean result = TRUE;

  for (GList *elem = channel_ids; elem && result; elem = elem->next)
    result = _flush_chunk_remainder(self, g_hash_table_lookup(self->channels, elem->data));

  g_list_free(channel_ids);
  return result;
}

static LogProtoStatus
_consume_input(LogProtoSplunkS2SServer *self)
{
  guchar buf[READ_CHUNK_SIZE];

  for (gint i = 0; i < MAX_READS_PER_POLL; i++)
    {
      gssize rc = log_transport_stack_read(&self->super.transport_stack, buf, sizeof(buf), NULL);
      if (rc < 0)
        {
          if (errno == EAGAIN || errno == EINTR)
            return LPS_SUCCESS;

          msg_error("splunk-s2s: error reading data",
                    evt_tag_int("fd", self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          return LPS_ERROR;
        }

      if (rc == 0)
        {
          if (self->in_buf->len - self->in_pos > 0)
            msg_notice("splunk-s2s: EOF occurred mid-packet, the tail of the stream is discarded",
                       evt_tag_int("fd", self->super.transport_stack.fd),
                       evt_tag_long("discarded_bytes", self->in_buf->len - self->in_pos));
          if (!_flush_all_chunk_remainders(self))
            return LPS_ERROR;
          self->eof_seen = TRUE;
          return LPS_SUCCESS;
        }

      g_string_append_len(self->in_buf, (const gchar *) buf, rc);
      LogProtoStatus status = _decode_available(self);
      if (status != LPS_SUCCESS)
        return status;

      if (!g_queue_is_empty(&self->pending_messages))
        return LPS_SUCCESS;
    }

  return LPS_SUCCESS;
}

/* transport_mapper_inet delegates starting TLS to the logproto plugin: if
 * tls() is configured, a TLS factory is on the stack but the active
 * transport is still the plain socket */
static gboolean
_start_tls_if_configured(LogProtoSplunkS2SServer *self)
{
  LogTransportStack *stack = &self->super.transport_stack;

  if (stack->active_transport == LOG_TRANSPORT_TLS || !stack->transport_factories[LOG_TRANSPORT_TLS])
    return TRUE;

  return log_transport_stack_switch(stack, LOG_TRANSPORT_TLS);
}

static void
_fill_aux(SplunkS2SServerMessage *message, LogTransportAuxData *aux)
{
  if (message->timestamp)
    {
      struct timespec timestamp = { .tv_sec = (time_t) message->timestamp, .tv_nsec = 0 };
      log_transport_aux_data_set_timestamp(aux, &timestamp);
    }

  if (message->index)
    log_transport_aux_data_add_nv_pair(aux, ".splunk.index", message->index);
  if (message->source)
    log_transport_aux_data_add_nv_pair(aux, ".splunk.source", message->source);
  if (message->sourcetype)
    log_transport_aux_data_add_nv_pair(aux, ".splunk.sourcetype", message->sourcetype);
  if (message->host)
    {
      log_transport_aux_data_add_nv_pair(aux, ".splunk.host", message->host);
      log_transport_aux_data_add_nv_pair(aux, "HOST", message->host);
    }
}

static LogProtoStatus
log_proto_splunk_s2s_server_fetch(LogProtoServer *s, const guchar **msg, gsize *msg_len, gboolean *may_read,
                                  LogTransportAuxData *aux, Bookmark *bookmark)
{
  LogProtoSplunkS2SServer *self = (LogProtoSplunkS2SServer *) s;

  *msg = NULL;
  *msg_len = 0;

  if (self->broken)
    return LPS_ERROR;

  if (!self->tls_checked)
    {
      self->tls_checked = TRUE;
      if (!_start_tls_if_configured(self))
        return _broken(self, LPS_ERROR);
    }

  if (self->current_message)
    {
      _message_free(self->current_message);
      self->current_message = NULL;
    }

  if (_flush_out_buf(self) == LPS_ERROR)
    return _broken(self, LPS_ERROR);

  if (g_queue_is_empty(&self->pending_messages) && !self->eof_seen && *may_read)
    {
      LogProtoStatus status = _consume_input(self);
      if (status != LPS_SUCCESS)
        return _broken(self, status);

      /* freshly decoded events may have queued acknowledgements */
      if (_flush_out_buf(self) == LPS_ERROR)
        return _broken(self, LPS_ERROR);
    }

  SplunkS2SServerMessage *message = g_queue_pop_head(&self->pending_messages);
  if (message)
    {
      self->current_message = message;
      _fill_aux(message, aux);
      *msg = (const guchar *) message->raw->str;
      *msg_len = message->raw->len;
      return LPS_SUCCESS;
    }

  if (self->eof_seen)
    {
      if (!self->signature_seen)
        msg_error("splunk-s2s: connection closed during the handshake",
                  evt_tag_int("fd", self->super.transport_stack.fd));
      return LPS_EOF;
    }
  return LPS_SUCCESS;
}

static LogProtoPrepareAction
log_proto_splunk_s2s_server_poll_prepare(LogProtoServer *s, GIOCondition *cond, gint *timeout)
{
  LogProtoSplunkS2SServer *self = (LogProtoSplunkS2SServer *) s;

  *cond = 0;

  if (!g_queue_is_empty(&self->pending_messages))
    return LPPA_FORCE_SCHEDULE_FETCH;

  if (log_transport_stack_poll_prepare(&self->super.transport_stack, cond))
    return LPPA_FORCE_SCHEDULE_FETCH;

  if (*cond == 0)
    *cond = G_IO_IN;
  if (_out_buf_pending(self))
    *cond |= G_IO_OUT;

  return LPPA_POLL_IO;
}

static void
log_proto_splunk_s2s_server_free(LogProtoServer *s)
{
  LogProtoSplunkS2SServer *self = (LogProtoSplunkS2SServer *) s;

  if (self->current_message)
    _message_free(self->current_message);

  SplunkS2SServerMessage *message;
  while ((message = g_queue_pop_head(&self->pending_messages)))
    _message_free(message);

  g_hash_table_destroy(self->channels);
  g_string_free(self->in_buf, TRUE);
  g_string_free(self->out_buf, TRUE);
  log_proto_server_free_method(s);
}

LogProtoServer *
log_proto_splunk_s2s_server_new(LogTransport *transport, const LogProtoServerOptions *options)
{
  LogProtoSplunkS2SServer *self = g_new0(LogProtoSplunkS2SServer, 1);

  log_proto_server_init(&self->super, transport, options);
  self->super.fetch = log_proto_splunk_s2s_server_fetch;
  self->super.poll_prepare = log_proto_splunk_s2s_server_poll_prepare;
  self->super.validate_options = log_proto_server_validate_options_method;
  self->super.free_fn = log_proto_splunk_s2s_server_free;

  self->phase = SS2S_SRV_PROTO_HEADER1;
  self->in_buf = g_string_sized_new(READ_CHUNK_SIZE);
  self->out_buf = g_string_sized_new(256);
  self->channels = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, _channel_free);
  g_queue_init(&self->pending_messages);

  return &self->super;
}

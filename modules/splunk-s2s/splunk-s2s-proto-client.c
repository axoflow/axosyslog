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

#include "splunk-s2s-proto-client.h"
#include "splunk-s2s-protocol.h"

#include "logmsg/logmsg.h"
#include "messages.h"
#include "scratch-buffers.h"
#include "uuid.h"

#include <errno.h>
#include <string.h>
#include <sys/utsname.h>

#define MAX_HANDSHAKE_FRAME_LEN (1024 * 1024)
#define MAX_READS_PER_POLL 100
#define DEFAULT_CHANNEL_ID 1
#define DEFAULT_IDENTIFIER "axosyslog"
#define DEFAULT_INDEX "main"
#define DEFAULT_SOURCE "axosyslog"
#define DEFAULT_SOURCETYPE "axosyslog"

/*
 * One channel serves the whole connection: the MetaData:* fields on each
 * event override the channel headers on the indexer, so no per-tuple
 * channel bookkeeping is needed.
 *
 * The signature requests indexer acknowledgements (ack=1): every event is
 * assigned an increasing id (the firstid header slot, starting at 2 as
 * forwarders do) and the indexer confirms durably received ids on the
 * reverse channel as 0xfb singles and 0xfa inclusive ranges.
 */
typedef enum
{
  SS2S_SEND_HELLO,   /* header1 + the v3 signature frame */
  SS2S_AWAIT_REPLY,  /* the indexer's v3 signature reply */
  SS2S_SEND_INFO,    /* forwarder info advertising v4 + the default channel */
  SS2S_CONNECTED,
  SS2S_BROKEN,
} LogProtoSplunkS2SClientState;

typedef struct _SplunkS2SAckRange
{
  guint64 lo;
  guint64 hi;
} SplunkS2SAckRange;

typedef struct _LogProtoSplunkS2SClient
{
  LogProtoClient super;
  LogProtoSplunkS2SClientState state;

  GString *out_buf;
  gsize out_pos;

  guchar reply_len_buf[4];
  gsize reply_len_pos;
  guint32 reply_remaining;

  GString *in_buf;
  guint64 next_event_id;
  guint64 next_unacked_id;
  GArray *acked_ranges;

  gchar guid[48];
  gchar *forwarder_info;

  NVHandle index_handle;
  NVHandle source_handle;
  NVHandle sourcetype_handle;
  NVHandle host_handle;
} LogProtoSplunkS2SClient;

/* the connection is going down: whatever the indexer has not confirmed yet
 * is pushed back to the queue so it is resent on the next connection */
static LogProtoStatus
_broken(LogProtoSplunkS2SClient *self, LogProtoStatus status)
{
  if (self->state == SS2S_BROKEN)
    return status;

  self->state = SS2S_BROKEN;
  if (self->next_unacked_id < self->next_event_id)
    log_proto_client_msg_rewind(&self->super);
  return status;
}

static gboolean
_out_buf_pending(LogProtoSplunkS2SClient *self)
{
  return self->out_pos < self->out_buf->len;
}

static LogProtoStatus
_flush_out_buf(LogProtoSplunkS2SClient *self)
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

static void
_format_hello(LogProtoSplunkS2SClient *self)
{
  splunk_s2s_format_header1(self->out_buf, DEFAULT_IDENTIFIER, "0");
  splunk_s2s_format_v3_signature_frame(self->out_buf, SPLUNK_S2S_CAPABILITIES_SIGNATURE_ACK);
}

static void
_format_info_and_default_channel(LogProtoSplunkS2SClient *self)
{
  splunk_s2s_format_v3_forwarder_info_frame(self->out_buf, self->forwarder_info, self->guid,
                                            (guint64) (g_get_real_time() / G_USEC_PER_SEC));
  splunk_s2s_format_open_channel(self->out_buf, DEFAULT_CHANNEL_ID, DEFAULT_SOURCE, g_get_host_name(),
                                 DEFAULT_SOURCETYPE);
}

static LogProtoStatus
_consume_reply(LogProtoSplunkS2SClient *self)
{
  while (self->reply_len_pos < sizeof(self->reply_len_buf))
    {
      gssize rc = log_transport_stack_read(&self->super.transport_stack,
                                           self->reply_len_buf + self->reply_len_pos,
                                           sizeof(self->reply_len_buf) - self->reply_len_pos, NULL);
      if (rc < 0)
        {
          if (errno == EAGAIN || errno == EINTR)
            return LPS_PARTIAL;
          msg_error("splunk-s2s: I/O error while reading the handshake reply",
                    evt_tag_int("fd", self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          return LPS_ERROR;
        }
      if (rc == 0)
        {
          msg_error("splunk-s2s: connection closed during the handshake",
                    evt_tag_int("fd", self->super.transport_stack.fd));
          return LPS_ERROR;
        }

      self->reply_len_pos += rc;
      if (self->reply_len_pos == sizeof(self->reply_len_buf))
        {
          guint32 frame_len;
          splunk_s2s_parse_v3_frame_len(self->reply_len_buf, sizeof(self->reply_len_buf), &frame_len);
          if (frame_len > MAX_HANDSHAKE_FRAME_LEN)
            {
              msg_error("splunk-s2s: unexpected handshake reply length, peer is probably not a Splunk indexer",
                        evt_tag_int("fd", self->super.transport_stack.fd),
                        evt_tag_int("frame_len", frame_len));
              return LPS_ERROR;
            }
          self->reply_remaining = frame_len;
        }
    }

  while (self->reply_remaining > 0)
    {
      guchar scratch[4096];
      gsize chunk = MIN(self->reply_remaining, sizeof(scratch));

      gssize rc = log_transport_stack_read(&self->super.transport_stack, scratch, chunk, NULL);
      if (rc < 0)
        {
          if (errno == EAGAIN || errno == EINTR)
            return LPS_PARTIAL;
          msg_error("splunk-s2s: I/O error while reading the handshake reply",
                    evt_tag_int("fd", self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          return LPS_ERROR;
        }
      if (rc == 0)
        {
          msg_error("splunk-s2s: connection closed during the handshake",
                    evt_tag_int("fd", self->super.transport_stack.fd));
          return LPS_ERROR;
        }

      self->reply_remaining -= rc;
    }

  return LPS_SUCCESS;
}

/* transport_mapper_inet delegates starting TLS to the logproto plugin: if
 * tls() is configured, a TLS factory is on the stack but the active
 * transport is still the plain socket */
static gboolean
_start_tls_if_configured(LogProtoSplunkS2SClient *self)
{
  LogTransportStack *stack = &self->super.transport_stack;

  if (stack->active_transport == LOG_TRANSPORT_TLS || !stack->transport_factories[LOG_TRANSPORT_TLS])
    return TRUE;

  return log_transport_stack_switch(stack, LOG_TRANSPORT_TLS);
}

/* drive the handshake as far as the socket allows without blocking;
 * LPS_PARTIAL means the socket is not ready, poll_prepare() tells which way */
static LogProtoStatus
_handshake_step(LogProtoSplunkS2SClient *self)
{
  LogProtoStatus status;

  while (TRUE)
    {
      switch (self->state)
        {
        case SS2S_SEND_HELLO:
          if (!_out_buf_pending(self) && self->out_buf->len == 0)
            {
              if (!_start_tls_if_configured(self))
                return LPS_ERROR;
              _format_hello(self);
            }
          status = _flush_out_buf(self);
          if (status != LPS_SUCCESS)
            return status;
          self->state = SS2S_AWAIT_REPLY;
          self->reply_len_pos = 0;
          self->reply_remaining = 0;
          break;
        case SS2S_AWAIT_REPLY:
          status = _consume_reply(self);
          if (status != LPS_SUCCESS)
            return status;
          self->state = SS2S_SEND_INFO;
          _format_info_and_default_channel(self);
          break;
        case SS2S_SEND_INFO:
          status = _flush_out_buf(self);
          if (status != LPS_SUCCESS)
            return status;
          self->state = SS2S_CONNECTED;
          msg_debug("splunk-s2s: handshake finished",
                    evt_tag_int("fd", self->super.transport_stack.fd));
          break;
        case SS2S_CONNECTED:
          return LPS_SUCCESS;
        default:
          g_assert_not_reached();
        }
    }
}

static gboolean
log_proto_splunk_s2s_client_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond,
                                         gint *timeout)
{
  LogProtoSplunkS2SClient *self = (LogProtoSplunkS2SClient *) s;

  *idle_cond = G_IO_IN;

  switch (self->state)
    {
    case SS2S_SEND_HELLO:
    case SS2S_SEND_INFO:
      *cond = G_IO_OUT;
      return TRUE;
    case SS2S_AWAIT_REPLY:
      *cond = G_IO_IN;
      return TRUE;
    case SS2S_CONNECTED:
      *cond = G_IO_OUT | G_IO_IN;
      return _out_buf_pending(self);
    case SS2S_BROKEN:
      *cond = G_IO_IN;
      return FALSE;
    default:
      g_assert_not_reached();
    }
}

static gboolean
_register_ack_range(LogProtoSplunkS2SClient *self, guint64 lo, guint64 hi)
{
  if (lo > hi || hi >= self->next_event_id)
    return FALSE;

  guint i = 0;
  while (i < self->acked_ranges->len && g_array_index(self->acked_ranges, SplunkS2SAckRange, i).lo < lo)
    i++;
  SplunkS2SAckRange range = { lo, hi };
  g_array_insert_val(self->acked_ranges, i, range);

  for (i = i > 0 ? i - 1 : 0; i + 1 < self->acked_ranges->len; )
    {
      SplunkS2SAckRange *current = &g_array_index(self->acked_ranges, SplunkS2SAckRange, i);
      SplunkS2SAckRange *next = &g_array_index(self->acked_ranges, SplunkS2SAckRange, i + 1);

      if (next->lo <= current->hi + 1)
        {
          current->hi = MAX(current->hi, next->hi);
          g_array_remove_index(self->acked_ranges, i + 1);
        }
      else
        i++;
    }
  return TRUE;
}

/* the indexer acks out of order and coalesces later, only the contiguous
 * prefix of the id space is safe to report to the writer, whose backlog is
 * strictly FIFO */
static guint
_pop_confirmed_prefix(LogProtoSplunkS2SClient *self)
{
  guint count = 0;

  while (self->acked_ranges->len > 0)
    {
      SplunkS2SAckRange *first = &g_array_index(self->acked_ranges, SplunkS2SAckRange, 0);

      if (first->lo > self->next_unacked_id)
        break;

      if (first->hi >= self->next_unacked_id)
        {
          count += first->hi - self->next_unacked_id + 1;
          self->next_unacked_id = first->hi + 1;
        }
      g_array_remove_index(self->acked_ranges, 0);
    }
  return count;
}

static LogProtoStatus
_parse_ack_stream(LogProtoSplunkS2SClient *self)
{
  const guchar *buf = (const guchar *) self->in_buf->str;
  gsize len = self->in_buf->len;
  gsize pos = 0;

  while (pos < len)
    {
      guchar tag = buf[pos];
      gsize p = pos + 1;
      guint64 lo, hi;
      SplunkS2SParseResult result;

      if (tag == SPLUNK_S2S_PKT_ACK_ONE)
        {
          result = splunk_s2s_parse_varint(buf, len, &p, &lo);
          hi = lo;
        }
      else if (tag == SPLUNK_S2S_PKT_ACK_RANGE)
        {
          result = splunk_s2s_parse_varint(buf, len, &p, &lo);
          if (result == SPLUNK_S2S_PARSE_OK)
            result = splunk_s2s_parse_varint(buf, len, &p, &hi);
        }
      else
        {
          msg_error("splunk-s2s: unexpected packet from the indexer on the ack channel",
                    evt_tag_int("fd", self->super.transport_stack.fd),
                    evt_tag_printf("tag", "0x%02x", tag));
          return LPS_ERROR;
        }

      if (result == SPLUNK_S2S_PARSE_ERROR)
        {
          msg_error("splunk-s2s: invalid varint from the indexer on the ack channel",
                    evt_tag_int("fd", self->super.transport_stack.fd));
          return LPS_ERROR;
        }
      if (result != SPLUNK_S2S_PARSE_OK)
        break;

      if (!_register_ack_range(self, lo, hi))
        {
          msg_error("splunk-s2s: indexer acked an event id that was never sent",
                    evt_tag_int("fd", self->super.transport_stack.fd),
                    evt_tag_long("lo", lo),
                    evt_tag_long("hi", hi));
          return LPS_ERROR;
        }
      pos = p;
    }

  g_string_erase(self->in_buf, 0, pos);

  guint confirmed = _pop_confirmed_prefix(self);
  if (confirmed)
    log_proto_client_msg_ack(&self->super, confirmed);
  return LPS_SUCCESS;
}

/* consume the indexer's acknowledgements and watch for a closed peer */
static LogProtoStatus
_process_acks(LogProtoSplunkS2SClient *self)
{
  guchar buf[1024];

  for (gint i = 0; i < MAX_READS_PER_POLL; i++)
    {
      gssize rc = log_transport_stack_read(&self->super.transport_stack, buf, sizeof(buf), NULL);
      if (rc > 0)
        {
          g_string_append_len(self->in_buf, (const gchar *) buf, rc);
          LogProtoStatus status = _parse_ack_stream(self);
          if (status != LPS_SUCCESS)
            return status;
          continue;
        }
      if (rc == 0)
        {
          msg_notice("splunk-s2s: EOF occurred, the indexer closed the connection",
                     evt_tag_int("fd", self->super.transport_stack.fd));
          return LPS_EOF;
        }
      if (errno == EAGAIN || errno == EINTR)
        return LPS_SUCCESS;

      msg_error("splunk-s2s: error reading data",
                evt_tag_int("fd", self->super.transport_stack.fd),
                evt_tag_error(EVT_TAG_OSERROR));
      return LPS_ERROR;
    }

  return LPS_SUCCESS;
}

static LogProtoStatus
log_proto_splunk_s2s_client_process_in(LogProtoClient *s)
{
  LogProtoSplunkS2SClient *self = (LogProtoSplunkS2SClient *) s;

  if (self->state == SS2S_BROKEN)
    return LPS_ERROR;

  /* the indexer's reply arrives as readable input, so the handshake
   * advances from here too */
  if (self->state != SS2S_CONNECTED)
    {
      LogProtoStatus status = _handshake_step(self);
      if (status == LPS_ERROR || status == LPS_EOF)
        return _broken(self, status);
      return status == LPS_PARTIAL ? LPS_SUCCESS : status;
    }

  LogProtoStatus status = _process_acks(self);
  if (status == LPS_ERROR || status == LPS_EOF)
    return _broken(self, status);
  return status;
}

static LogProtoStatus
log_proto_splunk_s2s_client_flush(LogProtoClient *s)
{
  LogProtoSplunkS2SClient *self = (LogProtoSplunkS2SClient *) s;
  LogProtoStatus status;

  if (self->state == SS2S_BROKEN)
    return LPS_ERROR;

  if (self->state != SS2S_CONNECTED)
    status = _handshake_step(self);
  else
    status = _flush_out_buf(self);

  if (status == LPS_ERROR || status == LPS_EOF)
    return _broken(self, LPS_ERROR);
  return status == LPS_PARTIAL ? LPS_SUCCESS : status;
}

static const gchar *
_get_metadata_value(LogMessage *msg, NVHandle handle, const gchar *fallback, gssize *len)
{
  const gchar *value = log_msg_get_value_if_set(msg, handle, len);

  if (!value || *len == 0)
    {
      value = fallback;
      *len = strlen(fallback);
    }
  return value;
}

static void
_append_prefixed(GString *buffer, const gchar *prefix, const gchar *value, gssize value_len)
{
  g_string_assign(buffer, prefix);
  g_string_append_len(buffer, value, value_len);
}

static void
_encode_event(LogProtoSplunkS2SClient *self, LogMessage *msg, guint64 event_id, const guchar *raw, gsize raw_len)
{
  gssize len;

  GString *index = scratch_buffers_alloc();
  const gchar *value = _get_metadata_value(msg, self->index_handle, DEFAULT_INDEX, &len);
  g_string_append_len(index, value, len);

  GString *source = scratch_buffers_alloc();
  value = _get_metadata_value(msg, self->source_handle, DEFAULT_SOURCE, &len);
  _append_prefixed(source, "source::", value, len);

  GString *sourcetype = scratch_buffers_alloc();
  value = _get_metadata_value(msg, self->sourcetype_handle, DEFAULT_SOURCETYPE, &len);
  _append_prefixed(sourcetype, "sourcetype::", value, len);

  GString *host = scratch_buffers_alloc();
  value = log_msg_get_value_if_set(msg, self->host_handle, &len);
  if (!value || len == 0)
    value = log_msg_get_value(msg, LM_V_HOST, &len);
  if (!value || len == 0)
    {
      value = g_get_host_name();
      len = strlen(value);
    }
  _append_prefixed(host, "host::", value, len);

  SplunkS2SEventField fields[] =
  {
    { .name = "_MetaData:Index", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = index->str, .str_value_len = index->len },
    { .name = "MetaData:Sourcetype", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = sourcetype->str, .str_value_len = sourcetype->len },
    { .name = "MetaData:Source", .value_type = SPLUNK_S2S_VALUE_STR,
      .str_value = source->str, .str_value_len = source->len },
    { .name = "MetaData:Host", .value_type = SPLUNK_S2S_VALUE_STR, .str_value = host->str, .str_value_len = host->len },
  };
  splunk_s2s_format_event(self->out_buf, DEFAULT_CHANNEL_ID, SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER,
                          (guint64) msg->timestamps[LM_TS_STAMP].ut_sec, event_id,
                          fields, G_N_ELEMENTS(fields), (const gchar *) raw, raw_len);
}

static LogProtoStatus
log_proto_splunk_s2s_client_post(LogProtoClient *s, LogMessage *logmsg, guchar *msg, gsize msg_len,
                                 gboolean *consumed)
{
  LogProtoSplunkS2SClient *self = (LogProtoSplunkS2SClient *) s;

  *consumed = FALSE;

  if (self->state == SS2S_BROKEN)
    return LPS_ERROR;

  if (self->state != SS2S_CONNECTED)
    {
      LogProtoStatus status = _handshake_step(self);
      if (status == LPS_ERROR || status == LPS_EOF)
        return _broken(self, LPS_ERROR);
      if (self->state != SS2S_CONNECTED)
        return LPS_PARTIAL;
    }

  LogProtoStatus status = _flush_out_buf(self);
  if (status == LPS_ERROR)
    return _broken(self, status);
  if (_out_buf_pending(self))
    return LPS_PARTIAL;

  _encode_event(self, logmsg, self->next_event_id++, msg, msg_len);
  g_free(msg);
  *consumed = TRUE;

  status = _flush_out_buf(self);
  if (status == LPS_ERROR)
    return _broken(self, status);
  return status;
}

static void
log_proto_splunk_s2s_client_free(LogProtoClient *s)
{
  LogProtoSplunkS2SClient *self = (LogProtoSplunkS2SClient *) s;

  /* the proto can be torn down without an I/O error (e.g. a reopen), make
   * sure unconfirmed messages are requeued in that case too */
  _broken(self, LPS_SUCCESS);

  g_string_free(self->out_buf, TRUE);
  g_string_free(self->in_buf, TRUE);
  g_array_free(self->acked_ranges, TRUE);
  g_free(self->forwarder_info);
  log_proto_client_free_method(s);
}

static gchar *
_format_forwarder_info(const gchar *guid)
{
  /* the indexer parses and logs these fields; mirror the Splunk 10.2.2
   * universal forwarder the handshake is modeled on */
  struct utsname u;
  uname(&u);
  return g_strdup_printf("ForwarderInfo build=80b90d638de6 version=10.2.2 os=%s arch=%s "
                         "hostname=%s guid=%s fwdType=uf lastIndexer=None ssl=false protocolLevel=7",
                         u.sysname, u.machine, g_get_host_name(), guid);
}

LogProtoClient *
log_proto_splunk_s2s_client_new(LogTransport *transport, const LogProtoClientOptions *options)
{
  LogProtoSplunkS2SClient *self = g_new0(LogProtoSplunkS2SClient, 1);

  log_proto_client_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_splunk_s2s_client_poll_prepare;
  self->super.post = log_proto_splunk_s2s_client_post;
  self->super.process_in = log_proto_splunk_s2s_client_process_in;
  self->super.flush = log_proto_splunk_s2s_client_flush;
  self->super.free_fn = log_proto_splunk_s2s_client_free;

  self->state = SS2S_SEND_HELLO;
  self->out_buf = g_string_sized_new(2048);
  self->in_buf = g_string_sized_new(256);

  /* a forwarder's first event id is 2 */
  self->next_event_id = 2;
  self->next_unacked_id = 2;
  self->acked_ranges = g_array_new(FALSE, FALSE, sizeof(SplunkS2SAckRange));

  uuid_gen_random(self->guid, sizeof(self->guid));
  self->forwarder_info = _format_forwarder_info(self->guid);

  self->index_handle = log_msg_get_value_handle(".splunk.index");
  self->source_handle = log_msg_get_value_handle(".splunk.source");
  self->sourcetype_handle = log_msg_get_value_handle(".splunk.sourcetype");
  self->host_handle = log_msg_get_value_handle(".splunk.host");

  return &self->super;
}

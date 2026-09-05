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

#include "transport/transport-zlib.h"

#include "messages.h"

#include <errno.h>
#include <string.h>
#include <zlib.h>

/* the compressed octets a single read of the base transport may bring */
#define ZLIB_INPUT_BUFFER_SIZE 4096

const gchar *ZLIB_TRANSPORT_NAME = "zlib";

typedef struct _LogTransportZlib
{
  LogTransportAdapter super;

  z_stream deflate_stream;
  z_stream inflate_stream;
  gboolean deflate_initialized;
  gboolean inflate_initialized;

  /* compressed octets the base transport has not taken yet:
   * out_buf->data[out_pos .. out_buf->len) */
  GByteArray *out_buf;
  gsize out_pos;

  /* how much of the caller's buffer the deflate stream absorbed while a write
   * is blocked, which is what keeps the retry from compressing it twice */
  gsize blocked_len;

  /* the compressed input read from the base transport but not inflated yet
   * lives in in_buf, addressed by inflate_stream.next_in / avail_in */
  guchar in_buf[ZLIB_INPUT_BUFFER_SIZE];

  /* the last inflate() filled the caller's buffer and still had input left */
  gboolean more_output_available;
  /* the peer ended its stream: everything after it is delivered as EOF */
  gboolean stream_ended;
  /* a broken stream is reported to the log once, not on every read */
  gboolean error_logged;
} LogTransportZlib;

static LogTransport *
_get_base_transport(LogTransportZlib *self)
{
  return log_transport_stack_get_or_create_transport(self->super.super.stack, self->super.base_index);
}

static void
_log_zlib_error(LogTransportZlib *self, const gchar *message, z_stream *stream, gint rc)
{
  if (self->error_logged)
    return;
  self->error_logged = TRUE;

  msg_error(message,
            evt_tag_str("error", stream->msg ? stream->msg : "none"),
            evt_tag_int("rc", rc),
            evt_tag_int(EVT_TAG_FD, self->super.super.stack ? self->super.super.stack->fd : -1));
}

/****************************************************************************
 * Write
 ****************************************************************************/

/* the Z_SYNC_FLUSH is what makes everything so far decodable on arrival */
static gboolean
_deflate_into_out_buf(LogTransportZlib *self, const guchar *buf, gsize count, gint flush)
{
  self->deflate_stream.next_in = (Bytef *) buf;
  self->deflate_stream.avail_in = count;

  do
    {
      /* deflateBound() covers the deflate(), and the few further octets a
       * sync flush emits fit in the slack it leaves */
      gsize offset = self->out_buf->len;
      gsize space = deflateBound(&self->deflate_stream, self->deflate_stream.avail_in) + 64;

      g_byte_array_set_size(self->out_buf, offset + space);
      self->deflate_stream.next_out = self->out_buf->data + offset;
      self->deflate_stream.avail_out = space;

      gint rc = deflate(&self->deflate_stream, flush);
      g_byte_array_set_size(self->out_buf, offset + space - self->deflate_stream.avail_out);

      if (rc != Z_OK && rc != Z_BUF_ERROR)
        {
          _log_zlib_error(self, "Error compressing the zlib stream", &self->deflate_stream, rc);
          return FALSE;
        }
    }
  while (self->deflate_stream.avail_in > 0 || self->deflate_stream.avail_out == 0);

  return TRUE;
}

/* FALSE with EAGAIN means that some of it is still pending. */
static gboolean
_flush_out_buf(LogTransportZlib *self)
{
  LogTransport *base = _get_base_transport(self);

  while (self->out_pos < self->out_buf->len)
    {
      gssize rc = log_transport_write(base, self->out_buf->data + self->out_pos, self->out_buf->len - self->out_pos);

      if (rc > 0)
        {
          self->out_pos += rc;
          continue;
        }

      if (rc < 0 && errno != EAGAIN && errno != EINTR)
        return FALSE;

      /* the base may want the opposite direction, e.g. a TLS renegotiation */
      self->super.super.cond = base->cond != LTIO_NOTHING ? base->cond : LTIO_WRITE_WANTS_WRITE;
      errno = EAGAIN;
      return FALSE;
    }

  g_byte_array_set_size(self->out_buf, 0);
  self->out_pos = 0;

  return TRUE;
}

/* @count octets are now in the deflate stream; they count as written once
 * the compressed output reached the base, until then the retry contract of the
 * header applies. */
static gssize
_finish_write(LogTransportZlib *self, gsize count)
{
  self->blocked_len = count;
  if (!_flush_out_buf(self))
    return -1;

  self->blocked_len = 0;
  return (gssize) count;
}

static gssize
log_transport_zlib_write_method(LogTransport *s, const gpointer buf, gsize count)
{
  LogTransportZlib *self = (LogTransportZlib *) s;

  self->super.super.cond = LTIO_NOTHING;

  if (count == 0)
    return 0;

  if (count < self->blocked_len)
    {
      /* the deflate stream absorbed a longer prefix of the previous write, so
       * writing this one would lose the difference */
      msg_error("Internal error, a blocked zlib write was retried with fewer octets than it absorbed",
                evt_tag_int("absorbed", self->blocked_len),
                evt_tag_int("count", count));
      errno = EINVAL;
      return -1;
    }

  if (count > self->blocked_len
      && !_deflate_into_out_buf(self, (const guchar *) buf + self->blocked_len, count - self->blocked_len, Z_SYNC_FLUSH))
    {
      errno = EPIPE;
      return -1;
    }

  return _finish_write(self, count);
}

/* The deflate stream takes the vector piece by piece; the one sync flush at
 * the end makes the whole vector a single record for the peer. */
static gssize
log_transport_zlib_writev_method(LogTransport *s, struct iovec *iov, gint iov_count)
{
  LogTransportZlib *self = (LogTransportZlib *) s;
  gsize total = 0;

  self->super.super.cond = LTIO_NOTHING;

  for (gint i = 0; i < iov_count; i++)
    total += iov[i].iov_len;

  if (total == 0)
    return 0;

  if (total < self->blocked_len)
    {
      msg_error("Internal error, a blocked zlib write was retried with fewer octets than it absorbed",
                evt_tag_int("absorbed", self->blocked_len),
                evt_tag_int("count", total));
      errno = EINVAL;
      return -1;
    }

  if (total > self->blocked_len)
    {
      gsize skip = self->blocked_len;

      for (gint i = 0; i < iov_count; i++)
        {
          if (skip >= iov[i].iov_len)
            {
              skip -= iov[i].iov_len;
              continue;
            }
          if (!_deflate_into_out_buf(self, (const guchar *) iov[i].iov_base + skip, iov[i].iov_len - skip, Z_NO_FLUSH))
            {
              errno = EPIPE;
              return -1;
            }
          skip = 0;
        }
      if (!_deflate_into_out_buf(self, NULL, 0, Z_SYNC_FLUSH))
        {
          errno = EPIPE;
          return -1;
        }
    }

  return _finish_write(self, total);
}

/****************************************************************************
 * Read
 ****************************************************************************/

/* -1 is a broken stream, 0 means nothing could be produced from what we hold. */
static gssize
_inflate_into(LogTransportZlib *self, gpointer buf, gsize buflen)
{
  if (self->inflate_stream.avail_in == 0)
    {
      self->more_output_available = FALSE;
      return 0;
    }

  self->inflate_stream.next_out = (Bytef *) buf;
  self->inflate_stream.avail_out = buflen;

  gint rc = inflate(&self->inflate_stream, Z_SYNC_FLUSH);

  if (rc == Z_STREAM_END)
    self->stream_ended = TRUE;
  else if (rc != Z_OK && rc != Z_BUF_ERROR)
    {
      _log_zlib_error(self, "Error decompressing the zlib stream", &self->inflate_stream, rc);
      errno = EPROTO;
      return -1;
    }

  self->more_output_available = (self->inflate_stream.avail_in > 0 && self->inflate_stream.avail_out == 0);

  return (gssize) (buflen - self->inflate_stream.avail_out);
}

static gssize
log_transport_zlib_read_method(LogTransport *s, gpointer buf, gsize buflen, LogTransportAuxData *aux)
{
  LogTransportZlib *self = (LogTransportZlib *) s;
  LogTransport *base = _get_base_transport(self);

  self->super.super.cond = LTIO_NOTHING;

  if (buflen == 0)
    return 0;

  while (TRUE)
    {
      gssize produced = _inflate_into(self, buf, buflen);

      if (produced < 0)
        return -1;
      if (produced > 0)
        return produced;

      /* everything the peer sent is delivered, so its end of stream is ours */
      if (self->stream_ended)
        return 0;

      /* what inflate() did not consume moves to the front, so that the read
       * below appends to it */
      gsize remaining = self->inflate_stream.avail_in;
      if (remaining > 0)
        memmove(self->in_buf, self->inflate_stream.next_in, remaining);
      self->inflate_stream.next_in = self->in_buf;
      self->inflate_stream.avail_in = remaining;

      if (remaining == sizeof(self->in_buf))
        {
          _log_zlib_error(self, "Error decompressing the zlib stream, no progress on a full input buffer",
                          &self->inflate_stream, Z_BUF_ERROR);
          errno = EPROTO;
          return -1;
        }

      gssize rc = log_transport_read(base, self->in_buf + remaining, sizeof(self->in_buf) - remaining, aux);
      if (rc > 0)
        {
          self->inflate_stream.avail_in = remaining + rc;
          continue;
        }

      /* end of input or an error of the base: its errno and cond stand */
      self->super.super.cond = base->cond;
      return rc;
    }
}

/* the input we hold may still inflate into something, with nothing arriving */
static gboolean
log_transport_zlib_has_pending_input_method(LogTransport *s)
{
  LogTransportZlib *self = (LogTransportZlib *) s;

  return self->more_output_available;
}

/****************************************************************************
 * Construction
 ****************************************************************************/

static void
log_transport_zlib_free_method(LogTransport *s)
{
  LogTransportZlib *self = (LogTransportZlib *) s;

  if (self->deflate_initialized)
    deflateEnd(&self->deflate_stream);
  if (self->inflate_initialized)
    inflateEnd(&self->inflate_stream);
  g_byte_array_free(self->out_buf, TRUE);

  log_transport_adapter_free_method(s);
}

LogTransport *
log_transport_zlib_new(LogTransportIndex base_index, gint level)
{
  LogTransportZlib *self = g_new0(LogTransportZlib, 1);

  log_transport_adapter_init_instance(&self->super, ZLIB_TRANSPORT_NAME, base_index);
  self->super.super.read = log_transport_zlib_read_method;
  self->super.super.write = log_transport_zlib_write_method;
  self->super.super.writev = log_transport_zlib_writev_method;
  self->super.super.has_pending_input = log_transport_zlib_has_pending_input_method;
  self->super.super.free_fn = log_transport_zlib_free_method;

  self->out_buf = g_byte_array_new();

  gint rc = deflateInit(&self->deflate_stream, level);
  if (rc != Z_OK)
    {
      _log_zlib_error(self, "Error initializing the zlib compressor", &self->deflate_stream, rc);
      log_transport_free(&self->super.super);
      return NULL;
    }
  self->deflate_initialized = TRUE;

  rc = inflateInit(&self->inflate_stream);
  if (rc != Z_OK)
    {
      _log_zlib_error(self, "Error initializing the zlib decompressor", &self->inflate_stream, rc);
      log_transport_free(&self->super.super);
      return NULL;
    }
  self->inflate_initialized = TRUE;

  return &self->super.super;
}

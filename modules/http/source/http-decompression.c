/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

#include "http-decompression.h"
#include "syslog-ng.h"

#include <string.h>

#ifdef SYSLOG_NG_HAVE_ZLIB
#include <zlib.h>

static HTTPDecompressResult
_inflate(GByteArray *dst, const guint8 *src, gsize src_len, gint window_bits, gsize max_output_size)
{
  z_stream strm;
  memset(&strm, 0, sizeof(strm));

  if (inflateInit2(&strm, window_bits) != Z_OK)
    return HTTP_DECOMPRESS_INVALID;

  strm.next_in = (Bytef *) src;
  strm.avail_in = src_len;

  guint8 out[16384];
  gint ret;
  do
    {
      strm.next_out = out;
      strm.avail_out = sizeof(out);

      ret = inflate(&strm, Z_NO_FLUSH);
      /* Z_BUF_ERROR here means the input ended mid-stream, since a full output
       * buffer is provided on every call. */
      if (ret != Z_OK && ret != Z_STREAM_END)
        {
          inflateEnd(&strm);
          return HTTP_DECOMPRESS_INVALID;
        }

      gsize produced = sizeof(out) - strm.avail_out;
      if (produced > max_output_size - dst->len)
        {
          inflateEnd(&strm);
          return HTTP_DECOMPRESS_TOO_LARGE;
        }
      g_byte_array_append(dst, out, produced);
    }
  while (ret != Z_STREAM_END);

  inflateEnd(&strm);
  return HTTP_DECOMPRESS_OK;
}

static HTTPDecompressResult
_decode(GByteArray *dst, const guint8 *src, gsize src_len, const gint *window_bits, gsize n_window_bits,
        gsize max_output_size)
{
  HTTPDecompressResult result = HTTP_DECOMPRESS_INVALID;
  for (gsize i = 0; i < n_window_bits; i++)
    {
      g_byte_array_set_size(dst, 0);
      result = _inflate(dst, src, src_len, window_bits[i], max_output_size);
      if (result != HTTP_DECOMPRESS_INVALID)
        break;
    }
  return result;
}
#endif

static gboolean
_is_identity(const gchar *encoding)
{
  return !encoding || !encoding[0] || g_ascii_strcasecmp(encoding, "identity") == 0;
}

HTTPDecompressResult
http_message_decode_content_encoding(HTTPMessage *self, gsize max_output_size)
{
  GString *encoding = http_message_get_header(self, "content-encoding");
  if (_is_identity(encoding ? encoding->str : NULL))
    {
      if (encoding)
        g_string_free(encoding, TRUE);
      return HTTP_DECOMPRESS_OK;
    }

#ifdef SYSLOG_NG_HAVE_ZLIB
  /* 32 + 15 auto-detects a zlib or gzip header; -15 handles headerless raw
   * deflate, which some clients send for Content-Encoding: deflate.
   * The order matters: raw inflate of a zlib-wrapped stream is not
   * guaranteed to fail, so the auto-detecting attempt must come first. */
  static const gint gzip_window_bits[] = { 32 + 15 };
  static const gint deflate_window_bits[] = { 32 + 15, -15 };

  const gint *window_bits;
  gsize n_window_bits;
  if (g_ascii_strcasecmp(encoding->str, "gzip") == 0 || g_ascii_strcasecmp(encoding->str, "x-gzip") == 0)
    {
      window_bits = gzip_window_bits;
      n_window_bits = G_N_ELEMENTS(gzip_window_bits);
    }
  else if (g_ascii_strcasecmp(encoding->str, "deflate") == 0)
    {
      window_bits = deflate_window_bits;
      n_window_bits = G_N_ELEMENTS(deflate_window_bits);
    }
  else
    {
      g_string_free(encoding, TRUE);
      return HTTP_DECOMPRESS_UNSUPPORTED;
    }
  g_string_free(encoding, TRUE);

  const GByteArray *body = http_message_get_body(self);
  GByteArray *decoded = g_byte_array_new();
  HTTPDecompressResult result = _decode(decoded, body->data, body->len, window_bits, n_window_bits, max_output_size);
  if (result != HTTP_DECOMPRESS_OK)
    {
      g_byte_array_free(decoded, TRUE);
      return result;
    }

  http_message_take_body(self, decoded);
  return HTTP_DECOMPRESS_OK;
#else
  g_string_free(encoding, TRUE);
  return HTTP_DECOMPRESS_UNSUPPORTED;
#endif
}

HTTPStatusCode
http_decompress_result_to_status_code(HTTPDecompressResult result)
{
  switch (result)
    {
    case HTTP_DECOMPRESS_UNSUPPORTED:
      return HTTP_UNSUPPORTED_MEDIA_TYPE;
    case HTTP_DECOMPRESS_TOO_LARGE:
      return HTTP_PAYLOAD_TOO_LARGE;
    default:
      return HTTP_BAD_REQUEST;
    }
}

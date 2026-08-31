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

#ifndef HTTP_DECOMPRESSION_H_INCLUDED
#define HTTP_DECOMPRESSION_H_INCLUDED

#include "http/http-message.h"

typedef enum
{
  HTTP_DECOMPRESS_OK,
  HTTP_DECOMPRESS_UNSUPPORTED,   /* Content-Encoding we can not decode */
  HTTP_DECOMPRESS_INVALID,       /* corrupt or truncated compressed stream */
  HTTP_DECOMPRESS_TOO_LARGE,     /* decompressed output exceeds max_output_size */
} HTTPDecompressResult;

HTTPDecompressResult http_message_decode_content_encoding(HTTPMessage *self, gsize max_output_size);
HTTPStatusCode http_decompress_result_to_status_code(HTTPDecompressResult result);

#endif

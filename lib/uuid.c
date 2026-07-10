/*
 * Copyright (c) 2010-2012 Balabit
 * Copyright (c) 2010-2012 Balázs Scheidler
 * Copyright (c) 2012 Gergely Nagy <algernon@balabit.hu>
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
 */

#include "uuid.h"

#include <openssl/rand.h>

static void
_format_uuid(const guchar *bytes, gchar *buf, gsize buflen)
{
  g_snprintf(buf, buflen,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5],
             bytes[6], bytes[7],
             bytes[8], bytes[9],
             bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

void
uuid_gen_random(gchar *buf, gsize buflen)
{
  /* RFC 4122 version 4 (random) UUID; flat byte buffer avoids union-access
   * and out-of-bounds warnings from static analyzers, writes over the 16 bytes,
   * and needs no htons(). */
  guchar rnd[16];

  RAND_bytes(rnd, sizeof(rnd));

  rnd[6] = (rnd[6] & 0x0F) | 0x40; /* version 4: top nibble of byte 6 */
  rnd[8] = (rnd[8] & 0x3F) | 0x80; /* RFC 4122 variant: top two bits of byte 8 */

  _format_uuid(rnd, buf, buflen);
}

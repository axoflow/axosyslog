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

  g_snprintf(buf, buflen,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             rnd[0], rnd[1], rnd[2], rnd[3],
             rnd[4], rnd[5],
             rnd[6], rnd[7],
             rnd[8], rnd[9],
             rnd[10], rnd[11], rnd[12], rnd[13], rnd[14], rnd[15]);
}

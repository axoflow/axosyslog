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
#include "timeutils/unixtime.h"

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

#define UUID_V7_COUNTER_LIMIT (G_GUINT64_CONSTANT(1) << 42)

static GMutex uuid_v7_lock;
static guint64 uuid_v7_ms;
static guint64 uuid_v7_counter;

static guint64
_uuid_v7_counter_seed(void)
{
  guint64 seed;
  RAND_bytes((guchar *) &seed, sizeof(seed));
  return seed >> 23;
}

void
uuid_gen_v7(gchar *buf, gsize buflen)
{
  UnixTime now;
  unix_time_set_now(&now);
  guint64 unix_ms = unix_time_to_unix_epoch_usec(now) / 1000;

  g_mutex_lock(&uuid_v7_lock);
  if (unix_ms > uuid_v7_ms)
    {
      uuid_v7_ms = unix_ms;
      uuid_v7_counter = _uuid_v7_counter_seed();
    }
  else if (++uuid_v7_counter == UUID_V7_COUNTER_LIMIT)
    {
      uuid_v7_ms++;
      uuid_v7_counter = _uuid_v7_counter_seed();
    }
  guint64 ms = uuid_v7_ms;
  guint64 counter = uuid_v7_counter;
  g_mutex_unlock(&uuid_v7_lock);

  guchar bytes[16];
  RAND_bytes(bytes + 12, 4);

  bytes[0] = (ms >> 40) & 0xFF;
  bytes[1] = (ms >> 32) & 0xFF;
  bytes[2] = (ms >> 24) & 0xFF;
  bytes[3] = (ms >> 16) & 0xFF;
  bytes[4] = (ms >> 8) & 0xFF;
  bytes[5] = ms & 0xFF;
  bytes[6] = 0x70 | ((counter >> 38) & 0x0F);
  bytes[7] = (counter >> 30) & 0xFF;
  bytes[8] = 0x80 | ((counter >> 24) & 0x3F);
  bytes[9] = (counter >> 16) & 0xFF;
  bytes[10] = (counter >> 8) & 0xFF;
  bytes[11] = counter & 0xFF;

  _format_uuid(bytes, buf, buflen);
}

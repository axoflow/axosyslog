/*
 * Copyright (c) 2002-2015 Balabit
 * Copyright (c) 2015 Viktor Juhasz <viktor.juhasz@balabit.com>
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

#ifndef G_SOCKADDR_SERIALIZE_H
#define G_SOCKADDR_SERIALIZE_H

#include "gsockaddr.h"
#include "serialize.h"

/* Remembers the last address read, so that consecutive messages carrying
 * the same address share a single GSockAddr instead of allocating one each.
 * The caller serializes the access to it. */
typedef struct _GSockAddrDeserializeCache
{
  GSockAddr *last;
} GSockAddrDeserializeCache;

gboolean g_sockaddr_serialize(SerializeArchive *sa, GSockAddr *addr);
gboolean g_sockaddr_deserialize(SerializeArchive *sa, GSockAddr **addr);
gboolean g_sockaddr_deserialize_cached(SerializeArchive *sa, GSockAddr **addr, GSockAddrDeserializeCache *cache);
void g_sockaddr_deserialize_cache_clear(GSockAddrDeserializeCache *cache);

#endif

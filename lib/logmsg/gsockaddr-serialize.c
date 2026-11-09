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

#include "gsockaddr-serialize.h"

#include <string.h>

static gboolean
_serialize_ipv4(GSockAddr *addr, SerializeArchive *sa)
{
  struct in_addr ina;
  ina = g_sockaddr_inet_get_address(addr);
  return serialize_write_blob(sa, (gchar *) &ina, sizeof(ina)) &&
         serialize_write_uint16(sa, htons(g_sockaddr_get_port(addr)));
}

#if SYSLOG_NG_ENABLE_IPV6
static gboolean
_serialize_ipv6(GSockAddr *addr, SerializeArchive *sa)
{
  struct in6_addr *in6a;
  in6a = g_sockaddr_inet6_get_address(addr);
  return serialize_write_blob(sa, (gchar *) in6a, sizeof(*in6a)) &&
         serialize_write_uint16(sa, htons(g_sockaddr_get_port(addr)));
}
#endif

gboolean
g_sockaddr_serialize(SerializeArchive *sa, GSockAddr *addr)
{
  if (!addr)
    {
      return serialize_write_uint16(sa, 0);
    }

  gboolean result = serialize_write_uint16(sa, addr->sa.sa_family);
  switch (addr->sa.sa_family)
    {
    case AF_INET:
    {
      result &= _serialize_ipv4(addr, sa);
      break;
    }
#if SYSLOG_NG_ENABLE_IPV6
    case AF_INET6:
    {
      result &= _serialize_ipv6(addr, sa);
      break;
    }
#endif
    case AF_UNIX:
    {
      break;
    }
    default:
    {
      result = FALSE;
      break;
    }
    }
  return result;
}

/* the constructors copy the sockaddr as a whole, so comparing all of its
 * bytes finds an identical address */
static GSockAddr *
_get_cached(GSockAddrDeserializeCache *cache, const struct sockaddr *sa, gsize salen)
{
  if (!cache || !cache->last)
    return NULL;
  if (cache->last->super.salen != salen || memcmp(&cache->last->sa, sa, salen) != 0)
    return NULL;
  return g_sockaddr_ref(cache->last);
}

static GSockAddr *
_store_cached(GSockAddrDeserializeCache *cache, GSockAddr *addr)
{
  if (cache)
    {
      g_sockaddr_unref(cache->last);
      cache->last = g_sockaddr_ref(addr);
    }
  return addr;
}

static gboolean
_deserialize_ipv4(SerializeArchive *sa, GSockAddr **addr, GSockAddrDeserializeCache *cache)
{
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));

  sin.sin_family = AF_INET;
  if (!serialize_read_blob(sa, (gchar *) &sin.sin_addr, sizeof(sin.sin_addr)) ||
      !serialize_read_uint16(sa, &sin.sin_port))
    return FALSE;

  *addr = _get_cached(cache, (struct sockaddr *) &sin, sizeof(sin));
  if (!*addr)
    *addr = _store_cached(cache, g_sockaddr_inet_new2(&sin));
  return TRUE;
}


#if SYSLOG_NG_ENABLE_IPV6
static gboolean
_deserialize_ipv6(SerializeArchive *sa, GSockAddr **addr, GSockAddrDeserializeCache *cache)
{
  gboolean result = FALSE;
  struct sockaddr_in6 sin6;
  memset(&sin6, 0, sizeof(sin6));

  sin6.sin6_family = AF_INET6;
  if (serialize_read_blob(sa, (gchar *) &sin6.sin6_addr, sizeof(sin6.sin6_addr)) &&
      serialize_read_uint16(sa, &sin6.sin6_port))
    {
      *addr = _get_cached(cache, (struct sockaddr *) &sin6, sizeof(sin6));
      if (!*addr)
        *addr = _store_cached(cache, g_sockaddr_inet6_new2(&sin6));
      result = TRUE;
    }
  return result;
}
#endif

gboolean
g_sockaddr_deserialize_cached(SerializeArchive *sa, GSockAddr **addr, GSockAddrDeserializeCache *cache)
{
  guint16 family;
  gboolean result = TRUE;

  if (!serialize_read_uint16(sa, &family))
    return FALSE;

  switch (family)
    {
    case 0:
      /* special case, no address was stored */
      *addr = NULL;
      break;
    case AF_INET:
    {
      result = _deserialize_ipv4(sa, addr, cache);
      break;
    }
#if SYSLOG_NG_ENABLE_IPV6
    case AF_INET6:
    {
      result = _deserialize_ipv6(sa, addr, cache);
      break;
    }
#endif
    case AF_UNIX:
      *addr = g_sockaddr_unix_new(NULL);
      break;
    default:
      result = FALSE;
      break;
    }
  return result;
}

gboolean
g_sockaddr_deserialize(SerializeArchive *sa, GSockAddr **addr)
{
  return g_sockaddr_deserialize_cached(sa, addr, NULL);
}

void
g_sockaddr_deserialize_cache_clear(GSockAddrDeserializeCache *cache)
{
  g_sockaddr_unref(cache->last);
  cache->last = NULL;
}

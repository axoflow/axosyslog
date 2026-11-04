/*
 * Copyright (c) 2026 Axoflow
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

#include <criterion/criterion.h>

#include "gsockaddr.h"

#include <stddef.h>
#include <string.h>

Test(gsockaddr, unshare_tolerates_null)
{
  GSockAddr *addr = NULL;

  g_sockaddr_unshare(&addr);
  cr_assert_null(addr);
}

Test(gsockaddr, unshare_keeps_a_sole_owner)
{
  GSockAddr *addr = g_sockaddr_inet_new("192.168.1.1", 514);
  GSockAddr *before = addr;

  g_sockaddr_unshare(&addr);
  cr_assert_eq(addr, before);
  g_sockaddr_unref(addr);
}

Test(gsockaddr, unshare_copies_a_shared_instance_and_releases_it)
{
  GSockAddr *shared = g_sockaddr_inet_new("192.168.1.1", 514);
  GSockAddr *addr = g_sockaddr_ref(shared);
  gchar expected[MAX_SOCKADDR_STRING], actual[MAX_SOCKADDR_STRING];

  g_sockaddr_unshare(&addr);
  cr_assert_neq(addr, shared);
  cr_assert_eq(g_atomic_counter_get(&addr->super.refcnt), 1);
  cr_assert_eq(g_atomic_counter_get(&shared->super.refcnt), 1);
  cr_assert_str_eq(g_sockaddr_format(addr, actual, sizeof(actual), GSA_FULL),
                   g_sockaddr_format(shared, expected, sizeof(expected), GSA_FULL));

  g_sockaddr_unref(addr);
  g_sockaddr_unref(shared);
}

static GSockAddr *
_unix_new2(const gchar *name, gsize name_len)
{
  struct sockaddr_un saun = { .sun_family = AF_UNIX };

  memcpy(saun.sun_path, name, name_len);
  return g_sockaddr_unix_new2(&saun, offsetof(struct sockaddr_un, sun_path) + name_len);
}

static void
_assert_address_equal(GSockAddr *a, GSockAddr *b, gboolean expected)
{
  cr_assert_eq(g_sockaddr_address_equal(a, b), expected);
  cr_assert_eq(g_sockaddr_address_equal(b, a), expected);
  g_sockaddr_unref(a);
  g_sockaddr_unref(b);
}

Test(gsockaddr, address_equal_handles_null)
{
  GSockAddr *addr = g_sockaddr_inet_new("192.168.1.1", 514);

  cr_assert(g_sockaddr_address_equal(NULL, NULL));
  cr_assert_not(g_sockaddr_address_equal(addr, NULL));
  cr_assert_not(g_sockaddr_address_equal(NULL, addr));
  cr_assert(g_sockaddr_address_equal(addr, addr));
  g_sockaddr_unref(addr);
}

Test(gsockaddr, address_equal_inet_ignores_the_port)
{
  _assert_address_equal(g_sockaddr_inet_new("192.168.1.1", 514), g_sockaddr_inet_new("192.168.1.1", 40000), TRUE);
  _assert_address_equal(g_sockaddr_inet_new("192.168.1.1", 514), g_sockaddr_inet_new("192.168.1.2", 514), FALSE);
}

Test(gsockaddr, address_equal_differs_for_different_families)
{
  _assert_address_equal(g_sockaddr_inet_new("127.0.0.1", 514), g_sockaddr_unix_new("/dev/log"), FALSE);
#if SYSLOG_NG_ENABLE_IPV6
  _assert_address_equal(g_sockaddr_inet_new("127.0.0.1", 514), g_sockaddr_inet6_new("::ffff:127.0.0.1", 514), FALSE);
#endif
}

#if SYSLOG_NG_ENABLE_IPV6
Test(gsockaddr, address_equal_inet6_ignores_the_port_but_not_the_scope)
{
  _assert_address_equal(g_sockaddr_inet6_new("fe80::1", 514), g_sockaddr_inet6_new("fe80::1", 40000), TRUE);
  _assert_address_equal(g_sockaddr_inet6_new("fe80::1", 514), g_sockaddr_inet6_new("fe80::2", 514), FALSE);

  GSockAddr *scoped = g_sockaddr_inet6_new("fe80::1", 514);
  g_sockaddr_inet6_get_sa(scoped)->sin6_scope_id = 2;
  _assert_address_equal(g_sockaddr_inet6_new("fe80::1", 514), scoped, FALSE);
}
#endif

Test(gsockaddr, address_equal_unix_compares_the_name)
{
  /* the kernel counts the terminating NUL of a pathname, SUN_LEN() does not */
  _assert_address_equal(g_sockaddr_unix_new("/dev/log"), _unix_new2("/dev/log", sizeof("/dev/log")), TRUE);
  _assert_address_equal(g_sockaddr_unix_new("/dev/log"), g_sockaddr_unix_new("/dev/log2"), FALSE);

  _assert_address_equal(g_sockaddr_unix_new(NULL), _unix_new2("", 0), TRUE);
  _assert_address_equal(g_sockaddr_unix_new(NULL), g_sockaddr_unix_new("/dev/log"), FALSE);

  /* abstract names start with a NUL and may contain further NULs */
  _assert_address_equal(_unix_new2("\0log\0a", 6), _unix_new2("\0log\0a", 6), TRUE);
  _assert_address_equal(_unix_new2("\0log\0a", 6), _unix_new2("\0log\0b", 6), FALSE);
  _assert_address_equal(_unix_new2("\0log", 4), _unix_new2("\0log\0", 5), FALSE);
  _assert_address_equal(_unix_new2("\0", 1), g_sockaddr_unix_new(NULL), FALSE);
}

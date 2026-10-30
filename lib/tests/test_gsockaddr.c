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

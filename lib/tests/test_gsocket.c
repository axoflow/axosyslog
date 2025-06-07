/*
 * Copyright (c) 2025 Axoflow
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include <criterion/criterion.h>
#include "gsocket.h"

Test(test_gsocket, test_inet_ntoa)
{
  gchar buf[32];
  struct in_addr ia;

  ia.s_addr = htonl(0xffffffff);

  g_inet_ntoa(buf, sizeof(buf), ia);
  cr_expect_str_eq(buf, "255.255.255.255");

  ia.s_addr = htonl(0x7f000001);

  g_inet_ntoa(buf, sizeof(buf), ia);
  cr_expect_str_eq(buf, "127.0.0.1");

  ia.s_addr = htonl(0xc0a80001);

  g_inet_ntoa(buf, sizeof(buf), ia);
  cr_expect_str_eq(buf, "192.168.0.1");
}
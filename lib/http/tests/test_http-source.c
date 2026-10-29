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

#include <criterion/criterion.h>

#include "apphook.h"
#include "http/source/http-source.h"
#include "socket/transport-mapper-inet.h"

static void
assert_transport_name_is_http(TransportMapper *transport_mapper)
{
  gsize len;
  const gchar *transport_name;

  cr_assert(transport_mapper_apply_transport(transport_mapper, configuration));

  transport_name = transport_mapper_get_transport_name(transport_mapper, &len);
  cr_assert_str_eq(transport_name, "http");
  cr_assert_eq(len, strlen("http"));
}

Test(http_source, transport_name_of_a_plain_connection)
{
  TransportMapper *transport_mapper = http_transport_mapper_new();

  assert_transport_name_is_http(transport_mapper);

  transport_mapper_free(transport_mapper);
}

Test(http_source, transport_name_of_a_tls_connection)
{
  TransportMapper *transport_mapper = http_transport_mapper_new();

  transport_mapper_set_transport(transport_mapper, "tls");
  transport_mapper_inet_set_tls_context((TransportMapperInet *) transport_mapper,
                                        tls_context_new(TM_SERVER, "dummy-location"));

  assert_transport_name_is_http(transport_mapper);

  transport_mapper_free(transport_mapper);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(http_source, .init = setup, .fini = teardown);

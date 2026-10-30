/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include <criterion/criterion.h>
#include "libtest/cr_template.h"

#include "apphook.h"
#include "plugin.h"
#include "cfg.h"
#include "maxminddb-helper.h"
#include "reloc.h"

#include <glib/gstdio.h>
#include <unistd.h>

#define TEST_DATABASE TOP_SRCDIR "/modules/geoip2/tests/test.mmdb"

static gchar *map_file;

/* the database map can only be overridden once, so every test gets its own
 * map file, its contents are set by the test */
static void
_override_map_file(void)
{
  gint fd = g_file_open_tmp("geoip-databases-XXXXXX.tsv", &map_file, NULL);
  cr_assert(fd >= 0);
  close(fd);
  override_installation_path_for(MMDB_DATABASE_MAP_FILE, map_file);
}

static void
_use_map_file(const gchar *contents)
{
  cr_assert(g_file_set_contents(map_file, contents, -1, NULL));
}

Test(tf_geoip, test_database)
{
  assert_template_format("$(geoip2 --database " TEST_DATABASE " 2.125.160.216)", "GB");
}

Test(tf_geoip, test_kind)
{
  _use_map_file("country\t/nonexistent/country.mmdb\n"
                "country\t" TEST_DATABASE "\n");

  assert_template_format("$(geoip2 --kind country 2.125.160.216)", "GB");
  assert_template_format("$(geoip2 --kind country --field location.time_zone 2.125.160.216)", "Europe/London");
}

Test(tf_geoip, test_kind_without_existing_database_is_empty)
{
  _use_map_file("country\t/nonexistent/country.mmdb\n");

  assert_template_format("$(geoip2 --kind country 2.125.160.216)", "");
}

Test(tf_geoip, test_unknown_kind_is_an_error)
{
  _use_map_file("country\t" TEST_DATABASE "\n");

  assert_template_failure("$(geoip2 --kind city 2.125.160.216)", "unknown GeoIP database kind city");
}

Test(tf_geoip, test_kind_and_database_are_mutually_exclusive)
{
  _use_map_file("country\t" TEST_DATABASE "\n");

  assert_template_failure("$(geoip2 --kind country --database " TEST_DATABASE " 2.125.160.216)",
                          "mutually exclusive");
}

static void
setup(void)
{
  app_startup();
  init_template_tests();
  cfg_load_module(configuration, "geoip2-plugin");
  _override_map_file();
}

static void
teardown(void)
{
  g_unlink(map_file);
  g_clear_pointer(&map_file, g_free);
  deinit_template_tests();
  app_shutdown();
}

TestSuite(tf_geoip, .init = setup, .fini = teardown);

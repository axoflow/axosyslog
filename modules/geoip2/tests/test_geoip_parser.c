/*
 * Copyright (c) 2017-2019 Balabit
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
#include "libtest/msg_parse_lib.h"

#include "geoip-parser.h"
#include "maxminddb-helper.h"
#include "reloc.h"
#include "apphook.h"
#include "scratch-buffers.h"

#include <glib/gstdio.h>
#include <unistd.h>

#define TEST_DATABASE TOP_SRCDIR "/modules/geoip2/tests/test.mmdb"

GlobalConfig *cfg;
LogParser *geoip_parser;
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

void
setup(void)
{
  app_startup();
  cfg = cfg_new_snippet();
  geoip_parser = maxminddb_parser_new(cfg);
  /*
   * The origin of the database:
   * https://github.com/maxmind/MaxMind-DB/blob/ea7cd314bb55879b8cb1a059c425d53ff3b9b6cc/test-data/GeoIP2-Precision-Enterprise-Test.mmdb
   * The same in JSON format:
   * https://github.com/maxmind/MaxMind-DB/blob/ea7cd314bb55879b8cb1a059c425d53ff3b9b6cc/source-data/GeoIP2-Precision-Enterprise-Test.json
   */
  geoip_parser_set_database_path(geoip_parser, TEST_DATABASE);
  _override_map_file();
}

void
teardown(void)
{
  g_unlink(map_file);
  g_clear_pointer(&map_file, g_free);
  log_pipe_deinit(&geoip_parser->super);
  log_pipe_unref(&geoip_parser->super);
  scratch_buffers_explicit_gc();
  cfg_free(cfg);
  app_shutdown();
}

static LogMessage *
parse_geoip_into_log_message_no_check(const gchar *template_format)
{
  LogMessage *msg;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  LogParser *cloned_parser;
  gboolean success;

  cloned_parser = (LogParser *) log_pipe_clone(&geoip_parser->super);

  LogTemplate *template = log_template_new(NULL, NULL);
  cr_assert(log_template_compile(template, template_format, NULL));
  log_parser_set_template(cloned_parser, template);

  log_pipe_init(&cloned_parser->super);
  msg = log_msg_new_empty();

  success = log_parser_process_message(cloned_parser, &msg, &path_options);
  if (!success)
    {
      log_msg_unref(msg);
      msg = NULL;
    }
  log_pipe_deinit(&cloned_parser->super);
  log_pipe_unref(&cloned_parser->super);
  return msg;
}

static LogMessage *
parse_geoip_into_log_message(const gchar *template_format)
{
  LogMessage *msg;

  msg = parse_geoip_into_log_message_no_check(template_format);
  cr_assert_not_null(msg, "expected geoip-parser success and it returned failure, template_format=%s", template_format);
  return msg;
}

Test(geoip2, template_is_mandatory)
{
  LogParser *geoip2_parser = maxminddb_parser_new(configuration);

  cr_assert_not(log_pipe_init(&geoip2_parser->super));

  log_pipe_unref(&geoip2_parser->super);
}

Test(geoip2, set_prefix)
{
  LogMessage *msg;

  geoip_parser_set_prefix(geoip_parser, ".prefix.");
  msg = parse_geoip_into_log_message("2.125.160.216");
  assert_log_message_value(msg, log_msg_get_value_handle(".prefix.country.iso_code"), "GB");
  log_msg_unref(msg);
}

Test(geoip2, empty_prefix)
{
  LogMessage *msg;

  geoip_parser_set_prefix(geoip_parser, "");
  msg = parse_geoip_into_log_message("2.125.160.216");
  assert_log_message_value(msg, log_msg_get_value_handle(".country.iso_code"), "GB");
  log_msg_unref(msg);
}

Test(geoip2, test_basic)
{
  LogMessage *msg;

  msg = parse_geoip_into_log_message("2.125.160.216");
  assert_log_message_value(msg, log_msg_get_value_handle(".geoip2.country.iso_code"), "GB");

  assert_log_message_value(msg, log_msg_get_value_handle(".geoip2.location.latitude"), "51.750000");
  assert_log_message_value(msg, log_msg_get_value_handle(".geoip2.location.longitude"), "-1.250000");

  log_msg_unref(msg);
}

Test(geoip2, test_reinit)
{
  LogParser *parser = (LogParser *) log_pipe_clone(&geoip_parser->super);
  LogTemplate *template = log_template_new(NULL, NULL);
  cr_assert(log_template_compile(template, "2.125.160.216", NULL));
  log_parser_set_template(parser, template);

  cr_assert(log_pipe_init(&parser->super));
  cr_assert(log_pipe_deinit(&parser->super));
  cr_assert(log_pipe_init(&parser->super));

  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  LogMessage *msg = log_msg_new_empty();
  cr_assert(log_parser_process_message(parser, &msg, &path_options));
  assert_log_message_value(msg, log_msg_get_value_handle(".geoip2.country.iso_code"), "GB");
  log_msg_unref(msg);

  log_pipe_deinit(&parser->super);
  log_pipe_unref(&parser->super);
}

Test(geoip2, test_kind)
{
  _use_map_file("country\t/nonexistent/country.mmdb\n"
                "country\t" TEST_DATABASE "\n");
  geoip_parser_set_database_path(geoip_parser, NULL);
  geoip_parser_set_kind(geoip_parser, "country");

  LogMessage *msg = parse_geoip_into_log_message("2.125.160.216");
  assert_log_message_value(msg, log_msg_get_value_handle(".geoip2.country.iso_code"), "GB");
  log_msg_unref(msg);
}

Test(geoip2, test_kind_without_existing_database_leaves_message_untouched)
{
  _use_map_file("country\t/nonexistent/country.mmdb\n");
  geoip_parser_set_database_path(geoip_parser, NULL);
  geoip_parser_set_kind(geoip_parser, "country");

  LogMessage *msg = parse_geoip_into_log_message("2.125.160.216");
  assert_log_message_value_unset(msg, log_msg_get_value_handle(".geoip2.country.iso_code"));
  log_msg_unref(msg);
}

static gboolean
_init_parser_with_kind(const gchar *kind, const gchar *database_path)
{
  LogParser *parser = maxminddb_parser_new(cfg);

  LogTemplate *template = log_template_new(NULL, NULL);
  cr_assert(log_template_compile(template, "2.125.160.216", NULL));
  log_parser_set_template(parser, template);
  geoip_parser_set_kind(parser, kind);
  geoip_parser_set_database_path(parser, database_path);

  gboolean result = log_pipe_init(&parser->super);
  if (result)
    log_pipe_deinit(&parser->super);
  log_pipe_unref(&parser->super);
  return result;
}

Test(geoip2, test_unknown_kind_is_an_error)
{
  _use_map_file("country\t" TEST_DATABASE "\n");

  cr_assert(_init_parser_with_kind("country", NULL));
  cr_assert_not(_init_parser_with_kind("city", NULL));
}

Test(geoip2, test_kind_and_database_are_mutually_exclusive)
{
  _use_map_file("country\t" TEST_DATABASE "\n");

  cr_assert_not(_init_parser_with_kind("country", TEST_DATABASE));
}

TestSuite(geoip2, .init = setup, .fini = teardown);

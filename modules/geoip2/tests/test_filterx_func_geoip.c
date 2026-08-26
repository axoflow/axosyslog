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
#include "libtest/parameterized.h"
#include "libtest/msg_parse_lib.h"
#include "libtest/filterx-lib.h"

#include "apphook.h"
#include "scratch-buffers.h"

#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/filterx-mapping.h"
#include "filterx/filterx-sequence.h"
#include "filterx/filterx-eval.h"
#include "filterx/expr-literal.h"
#include "filterx-func-geoip.h"
#include "maxminddb-helper.h"
#include "reloc.h"

#include <glib/gstdio.h>
#include <unistd.h>

/*
 * The origin of the database:
 * https://github.com/maxmind/MaxMind-DB/blob/ea7cd314bb55879b8cb1a059c425d53ff3b9b6cc/test-data/GeoIP2-Precision-Enterprise-Test.mmdb
 */
#define TEST_DATABASE TOP_SRCDIR "/modules/geoip2/tests/test.mmdb"
#define TEST_IP "2.125.160.216"

static gchar *database_map_file;

static FilterXExpr *
_construct(const gchar *ip, const gchar *database, const gchar *field, GError **error)
{
  GList *args = NULL;

  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(ip, -1))));

  if (database)
    args = g_list_append(args, filterx_function_arg_new("database",
                                                        filterx_literal_new(filterx_string_new(database, -1))));

  if (field)
    args = g_list_append(args, filterx_function_arg_new("field",
                                                        filterx_literal_new(filterx_string_new(field, -1))));

  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_geoip_new(filterx_function_args_new(args, &args_err), error);
  cr_assert_null(args_err);

  return func;
}

Test(filterx_func_geoip, test_empty_args_error)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_geoip_new(filterx_function_args_new(NULL, &args_err), &err);

  cr_assert_null(func);
  cr_assert_null(args_err);
  cr_assert_not_null(err);
  cr_assert(strstr(err->message, FILTERX_FUNC_GEOIP_USAGE) != NULL);
  g_error_free(err);
}

Test(filterx_func_geoip, test_nonexistent_database_error)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, "/nonexistent/geoip-test-database.mmdb", NULL, &err);

  cr_assert_null(func);
  cr_assert_not_null(err);
  g_error_free(err);
}

static FilterXExpr *
_construct_with_kind(const gchar *kind, const gchar *database, GError **error)
{
  GList *args = NULL;

  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(TEST_IP, -1))));
  args = g_list_append(args, filterx_function_arg_new("kind", filterx_literal_new(filterx_string_new(kind, -1))));
  if (database)
    args = g_list_append(args, filterx_function_arg_new("database",
                                                        filterx_literal_new(filterx_string_new(database, -1))));

  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_geoip_new(filterx_function_args_new(args, &args_err), error);
  cr_assert_null(args_err);

  return func;
}

Test(filterx_func_geoip, test_kind_and_database_are_mutually_exclusive)
{
  GError *err = NULL;
  FilterXExpr *func = _construct_with_kind("country", TEST_DATABASE, &err);

  cr_assert_null(func);
  cr_assert_not_null(err);
  cr_assert(strstr(err->message, "mutually exclusive") != NULL, "%s", err->message);
  g_error_free(err);
}

Test(filterx_func_geoip, test_unknown_kind_error)
{
  GError *err = NULL;
  FilterXExpr *func = _construct_with_kind("no-such-kind", NULL, &err);

  cr_assert_null(func);
  cr_assert_not_null(err);
  cr_assert(strstr(err->message, "unknown GeoIP database kind no-such-kind") != NULL, "%s", err->message);
  g_error_free(err);
}

static gchar *
_write_map_file(const gchar *contents)
{
  gchar *map_file = NULL;
  gint fd = g_file_open_tmp("geoip-databases-XXXXXX.tsv", &map_file, NULL);
  cr_assert(fd >= 0);
  close(fd);
  cr_assert(g_file_set_contents(map_file, contents, -1, NULL));
  return map_file;
}

static void
_use_map_file(const gchar *contents)
{
  cr_assert(g_file_set_contents(database_map_file, contents, -1, NULL));
}

static gchar *
_find_database(const gchar *map_contents, const gchar *kind, GPtrArray *candidates, GError **error)
{
  gchar *map_file = _write_map_file(map_contents);
  gchar *path = mmdb_find_database_by_kind(map_file, kind, candidates, error);
  g_unlink(map_file);
  g_free(map_file);
  return path;
}

Test(filterx_func_geoip, test_database_map_first_existing_path_wins)
{
  GPtrArray *candidates = g_ptr_array_new_with_free_func(g_free);
  GError *err = NULL;
  gchar *path = _find_database("# comment\n"
                               "\n"
                               "city\t/nonexistent/city.mmdb\n"
                               "country\t/nonexistent/country.mmdb\n"
                               "country\t" TEST_DATABASE "\n"
                               "country\t/nonexistent/later.mmdb\n",
                               "country", candidates, &err);

  cr_assert_null(err);
  cr_assert_str_eq(path, TEST_DATABASE);
  cr_assert_eq(candidates->len, 1);
  cr_assert_str_eq(g_ptr_array_index(candidates, 0), "/nonexistent/country.mmdb");

  g_free(path);
  g_ptr_array_free(candidates, TRUE);
}

Test(filterx_func_geoip, test_database_map_no_existing_path)
{
  GPtrArray *candidates = g_ptr_array_new_with_free_func(g_free);
  GError *err = NULL;
  gchar *path = _find_database("asn\t/nonexistent/a.mmdb\n"
                               "asn /nonexistent/b.mmdb\n",
                               "asn", candidates, &err);

  cr_assert_null(err);
  cr_assert_null(path);
  cr_assert_eq(candidates->len, 2);
  cr_assert_str_eq(g_ptr_array_index(candidates, 1), "/nonexistent/b.mmdb");

  g_ptr_array_free(candidates, TRUE);
}

Test(filterx_func_geoip, test_database_map_unknown_kind)
{
  GPtrArray *candidates = g_ptr_array_new_with_free_func(g_free);
  GError *err = NULL;
  gchar *path = _find_database("country\t" TEST_DATABASE "\n", "city", candidates, &err);

  cr_assert_null(err);
  cr_assert_null(path);
  cr_assert_eq(candidates->len, 0);

  g_ptr_array_free(candidates, TRUE);
}

Test(filterx_func_geoip, test_database_map_invalid_line)
{
  GPtrArray *candidates = g_ptr_array_new_with_free_func(g_free);
  GError *err = NULL;
  gchar *path = _find_database("country\trelative/path.mmdb\n", "country", candidates, &err);

  cr_assert_null(path);
  cr_assert_not_null(err);
  g_clear_error(&err);

  path = _find_database("country\n", "country", candidates, &err);
  cr_assert_null(path);
  cr_assert_not_null(err);
  g_error_free(err);

  g_ptr_array_free(candidates, TRUE);
}

Test(filterx_func_geoip, test_database_map_missing_file)
{
  GPtrArray *candidates = g_ptr_array_new_with_free_func(g_free);
  GError *err = NULL;
  gchar *path = mmdb_find_database_by_kind("/nonexistent/geoip-databases.tsv", "country", candidates, &err);

  cr_assert_null(path);
  cr_assert_not_null(err);
  g_error_free(err);
  g_ptr_array_free(candidates, TRUE);
}

Test(filterx_func_geoip, test_kind_uses_database_map)
{
  _use_map_file("country\t/nonexistent/country.mmdb\n"
                "country\t" TEST_DATABASE "\n");

  GError *err = NULL;
  FilterXExpr *func = _construct_with_kind("country", NULL, &err);
  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);
  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "GB", LM_VT_STRING);

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_kind_without_existing_database_returns_null)
{
  _use_map_file("country\t/nonexistent/country.mmdb\n");

  GError *err = NULL;
  FilterXExpr *func = _construct_with_kind("country", NULL, &err);
  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(null)));
  cr_assert_eq(filterx_eval_get_error_count(), 0);

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_shipped_database_map_is_valid)
{
  const gchar *map_file = TOP_SRCDIR "/modules/geoip2/geoip-databases.tsv";
  GPtrArray *candidates = g_ptr_array_new_with_free_func(g_free);
  GError *err = NULL;

  gchar *path = mmdb_find_database_by_kind(map_file, "no-such-kind", candidates, &err);
  cr_assert_null(err, "%s: %s", map_file, err ? err->message : "");
  cr_assert_null(path);

  g_ptr_array_free(candidates, TRUE);
}

Test(filterx_func_geoip, test_field_argument_must_be_string_literal)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(TEST_IP, -1))));
  args = g_list_append(args, filterx_function_arg_new("database",
                                                      filterx_literal_new(filterx_string_new(TEST_DATABASE, -1))));
  args = g_list_append(args, filterx_function_arg_new("field", filterx_literal_new(filterx_integer_new(5))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_geoip_new(filterx_function_args_new(args, &args_err), &err);

  cr_assert_null(func);
  cr_assert_null(args_err);
  cr_assert_not_null(err);
  cr_assert(strstr(err->message, FILTERX_FUNC_GEOIP_USAGE) != NULL);
  g_error_free(err);
}

Test(filterx_func_geoip, test_default_field_is_country_iso_code)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, TEST_DATABASE, NULL, &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "GB", LM_VT_STRING);

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_integer_field)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, TEST_DATABASE, "country.geoname_id", &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "2635167", LM_VT_INTEGER);

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_boolean_field)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, TEST_DATABASE, "country.is_in_european_union", &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "true", LM_VT_BOOLEAN);

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_double_field)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, TEST_DATABASE, "location.latitude", &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  assert_object_json_equals(obj, "51.75");

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_nested_dict_field)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, TEST_DATABASE, "postal", &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(filterx_ref_unwrap_ro(obj), &FILTERX_TYPE_NAME(mapping)));
  assert_object_json_equals(obj, "{\"code\":\"OX1\",\"confidence\":20}");

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_array_field)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, TEST_DATABASE, "subdivisions", &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(filterx_ref_unwrap_ro(obj), &FILTERX_TYPE_NAME(sequence)));

  guint64 len;
  cr_assert(filterx_object_len(obj, &len));
  cr_assert_eq(len, 2);

  FilterXObject *index_zero = filterx_integer_new(0);
  FilterXObject *first = filterx_object_get_subscript(obj, index_zero);
  cr_assert_not_null(first);
  cr_assert(filterx_object_is_type(filterx_ref_unwrap_ro(first), &FILTERX_TYPE_NAME(mapping)));

  FilterXObject *iso_code_key = filterx_string_new("iso_code", -1);
  FilterXObject *iso_code = filterx_object_get_subscript(first, iso_code_key);
  cr_assert_not_null(iso_code);
  assert_marshaled_object(iso_code, "ENG", LM_VT_STRING);

  filterx_object_unref(iso_code);
  filterx_object_unref(iso_code_key);
  filterx_object_unref(first);
  filterx_object_unref(index_zero);
  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_whole_entry)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, TEST_DATABASE, "", &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(filterx_ref_unwrap_ro(obj), &FILTERX_TYPE_NAME(mapping)));

  FilterXObject *country_key = filterx_string_new("country", -1);
  FilterXObject *country = filterx_object_get_subscript(obj, country_key);
  cr_assert_not_null(country);
  cr_assert(filterx_object_is_type(filterx_ref_unwrap_ro(country), &FILTERX_TYPE_NAME(mapping)));

  filterx_object_unref(country);
  filterx_object_unref(country_key);
  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_ipv6_lookup)
{
  GError *err = NULL;
  FilterXExpr *func = _construct("2001:218::", TEST_DATABASE, NULL, &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "JP", LM_VT_STRING);

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_ipv4_mapped_ipv6_lookup)
{
  GError *err = NULL;
  FilterXExpr *func = _construct("::ffff:" TEST_IP, TEST_DATABASE, NULL, &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "GB", LM_VT_STRING);

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

static const gchar *invalid_ips[] =
{
  "not-an-ip",
  "",
  "2.125.160.216 ",
  "2.125.160",
  "2.125.160.216/29",
  "2001:218::/32",
  "example.com",
};

StaticParameterizedTest(const gchar **ip, invalid_ips, filterx_func_geoip, test_invalid_ip_is_an_error)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(*ip, TEST_DATABASE, NULL, &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_null(obj, "ip=%s", *ip);
  cr_assert(filterx_eval_get_last_error(), "ip=%s", *ip);

  filterx_eval_clear_errors();
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_unknown_ip_returns_null)
{
  GError *err = NULL;
  FilterXExpr *func = _construct("127.0.0.1", TEST_DATABASE, NULL, &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(null)));

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

Test(filterx_func_geoip, test_unknown_field_returns_null)
{
  GError *err = NULL;
  FilterXExpr *func = _construct(TEST_IP, TEST_DATABASE, "does.not.exist", &err);

  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(null)));

  filterx_object_unref(obj);
  filterx_expr_unref(func);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();

  /* the database map can only be overridden once, so every test gets its own
   * map file, its contents are set by the test */
  database_map_file = _write_map_file("");
  override_installation_path_for(MMDB_DATABASE_MAP_FILE, database_map_file);
}

static void
teardown(void)
{
  g_unlink(database_map_file);
  g_clear_pointer(&database_map_file, g_free);
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_func_geoip, .init = setup, .fini = teardown);

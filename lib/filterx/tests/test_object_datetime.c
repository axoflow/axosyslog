/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "libtest/filterx-lib.h"

#include "filterx/object-datetime.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "filterx/object-string.h"
#include "filterx/filterx-sequence.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-function.h"
#include "filterx/expr-get-subscript.h"
#include "filterx/filterx-private.h"

Test(filterx_datetime, test_filterx_object_datetime_marshals_to_the_stored_values)
{
  UnixTime ut = { .ut_sec = 1701350398, .ut_usec = 123000, .ut_gmtoff = 3600 };

  FilterXObject *fobj = filterx_datetime_new(&ut);
  assert_marshaled_object(fobj, "1701350398.123000+01:00", LM_VT_DATETIME);
  filterx_object_unref(fobj);
}

Test(filterx_datetime, test_filterx_object_datetime_maps_to_the_right_json_value)
{
  UnixTime ut = { .ut_sec = 1701350398, .ut_usec = 123000, .ut_gmtoff = 3600 };
  FilterXObject *fobj = filterx_datetime_new(&ut);
  assert_object_json_equals(fobj, "\"1701350398.123000\"");
  filterx_object_unref(fobj);
}

Test(filterx_datetime, test_filterx_object_datetime_str_yields_unix_time)
{
  UnixTime ut = { .ut_sec = 1701350398, .ut_usec = 123000, .ut_gmtoff = 3600 };
  FilterXObject *fobj = filterx_datetime_new(&ut);
  assert_object_str_equals(fobj, "1701350398.123000");
  filterx_object_unref(fobj);
}

Test(filterx_datetime, test_filterx_object_datetime_repr_yields_isotime)
{
  UnixTime ut = { .ut_sec = 1701350398, .ut_usec = 123000, .ut_gmtoff = 3600 };
  FilterXObject *fobj = filterx_datetime_new(&ut);
  assert_object_repr_equals(fobj, "2023-11-30T14:19:58.123+01:00");
  filterx_object_unref(fobj);
}

Test(filterx_datetime, test_filterx_datetime_typecast_null_args)
{
  FilterXObject *obj = filterx_typecast_datetime(NULL, NULL, 0);
  cr_assert_null(obj);
}

Test(filterx_datetime, test_filterx_datetime_typecast_empty_args)
{
  FilterXObject *args[] = { NULL };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, 0);
  cr_assert_null(obj);
}

Test(filterx_datetime, test_filterx_datetime_typecast_null_arg)
{
  FilterXObject  *args[] = { NULL };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);
}

Test(filterx_datetime, test_filterx_datetime_typecast_null_object_arg)
{
  FilterXObject *args[] = { filterx_null_new() };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);
  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_datetime, test_filterx_datetime_typecast_from_int)
{
  // integer representation expected to be microsecond precision
  FilterXObject *args[] = { filterx_integer_new(1710762325395194) };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  UnixTime ut_expected = { .ut_sec = 1710762325, .ut_usec = 395194, .ut_gmtoff = 0 };

  UnixTime ut = filterx_datetime_get_value(obj);
  cr_assert(memcmp(&ut_expected, &ut, sizeof(UnixTime)) == 0);
  filterx_object_unref(obj);
  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_datetime, test_filterx_datetime_typecast_from_double)
{
  // double representation expected to be second precision
  FilterXObject *args[] = { filterx_double_new(1710762325.395194) };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  UnixTime ut_expected = { .ut_sec = 1710762325, .ut_usec = 395194, .ut_gmtoff = 0 };

  UnixTime ut = filterx_datetime_get_value(obj);
  cr_assert(memcmp(&ut_expected, &ut, sizeof(UnixTime)) == 0);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_datetime, test_filterx_datetime_typecast_from_string)
{
  // string representation expected to be rfc3339 standard
  FilterXObject *args[] = { filterx_string_new("2024-03-18T12:34:00Z", -1) };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  UnixTime ut_expected = { .ut_sec = 1710765240, .ut_usec = 0, .ut_gmtoff = 0 };

  UnixTime ut = filterx_datetime_get_value(obj);
  cr_assert(memcmp(&ut_expected, &ut, sizeof(UnixTime)) == 0);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_datetime, test_filterx_datetime_typecast_from_datetime)
{
  UnixTime ut = { .ut_sec = 1701350398, .ut_usec = 123000, .ut_gmtoff = 3600 };
  FilterXObject *args[] = { filterx_datetime_new(&ut) };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, G_N_ELEMENTS(args));

  cr_assert_eq(args[0], obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_datetime, test_filterx_datetime_repr_method)
{
  UnixTime ut = unix_time_from_unix_epoch_usec(3600000000);
  cr_assert(ut.ut_gmtoff == 0);
  cr_assert(ut.ut_usec == 0);
  cr_assert(ut.ut_sec == 3600);
  GString *repr = scratch_buffers_alloc();
  cr_assert(datetime_repr(&ut, repr));
  cr_assert_str_eq(repr->str, "1970-01-01T01:00:00.000+00:00");
}

Test(filterx_datetime, test_filterx_datetime_marshal)
{
  UnixTime ut = { .ut_sec = 1701350398, .ut_usec = 123000, .ut_gmtoff = 3600 };
  FilterXObject *obj = filterx_datetime_new(&ut);
  cr_assert_not_null(obj);

  LogMessageValueType lmvt;
  GString *repr = scratch_buffers_alloc();
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));
  cr_assert_str_eq(repr->str, "1701350398.123000+01:00");
}

Test(filterx_datetime, test_filterx_datetime_marshal_negative_offset)
{
  UnixTime ut = { .ut_sec = 1701350398, .ut_usec = 123000, .ut_gmtoff = -10800 };
  FilterXObject *obj = filterx_datetime_new(&ut);
  cr_assert_not_null(obj);

  LogMessageValueType lmvt;
  GString *repr = scratch_buffers_alloc();
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));
  cr_assert_str_eq(repr->str, "1701350398.123000-3:00");
}

Test(filterx_datetime, test_filterx_datetime_repr)
{
  FilterXObject *args[] = { filterx_string_new("2024-03-18T12:34:13+0900", -1) };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  assert_object_repr_equals(obj, "2024-03-18T12:34:13.000+09:00");

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_datetime, test_filterx_datetime_repr_isodate_Z)
{
  const gchar *test_time_str = "2024-03-18T12:34:00Z";
  FilterXObject *args[] = { filterx_string_new(test_time_str, -1) };

  FilterXObject *obj = filterx_typecast_datetime(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  assert_object_repr_equals(args[0], test_time_str);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_datetime, test_filterx_datetime_strptime_with_null_args)
{
  GError *error = NULL;
  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(NULL, &error), NULL);
  cr_assert_null(func_expr);
}

Test(filterx_datetime, test_filterx_datetime_strptime_without_args)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, NULL));
  args = g_list_append(args, filterx_function_arg_new(NULL, NULL));

  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(args, NULL), NULL);
  cr_assert_null(func_expr);
}

Test(filterx_datetime, test_filterx_datetime_strptime_without_timefmt)
{
  const gchar *test_time_str = "2024-04-08T10:11:12Z";
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(test_time_str, -1))));

  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(args, NULL), NULL);
  cr_assert_null(func_expr);
}

Test(filterx_datetime, test_filterx_datetime_strptime_non_matching_timefmt)
{
  const gchar *test_time_str = "2024-04-08T10:11:12Z";
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(test_time_str, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("non matching timefmt",
                                                      -1))));

  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(args, NULL), NULL);
  cr_assert(func_expr);

  FilterXObject *obj = init_and_eval_expr(func_expr);
  cr_assert(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(null)));

  filterx_object_unref(obj);
  filterx_expr_unref(func_expr);
}

Test(filterx_datetime, test_filterx_datetime_strptime_matching_timefmt)
{
  const gchar *test_time_str = "2024-04-08T10:11:12Z";
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(test_time_str, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(datefmt_isodate,
                                                      -1))));

  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(args, NULL), NULL);
  cr_assert(func_expr);

  FilterXObject *obj = init_and_eval_expr(func_expr);
  cr_assert(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  assert_object_repr_equals(obj, "2024-04-08T10:11:12.000+00:00");

  filterx_object_unref(obj);
  filterx_expr_unref(func_expr);
}

Test(filterx_datetime, test_filterx_datetime_strptime_matching_nth_timefmt)
{
  const gchar *test_time_str = "2024-04-08T10:11:12+01:00";
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(test_time_str, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("bad format 1", -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("bad format 2", -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(datefmt_isodate,
                                                      -1))));

  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(args, NULL), NULL);
  cr_assert(func_expr);

  FilterXObject *obj = init_and_eval_expr(func_expr);
  cr_assert(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  assert_object_repr_equals(obj, "2024-04-08T10:11:12.000+01:00");

  filterx_object_unref(obj);
  filterx_expr_unref(func_expr);
}

Test(filterx_datetime, test_filterx_datetime_strptime_non_matching_nth_timefmt)
{
  const gchar *test_time_str = "2024-04-08T10:11:12Z";
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(test_time_str, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("bad format 1", -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("bad format 2", -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("non matching fmt",
                                                      -1))));

  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(args, NULL), NULL);
  cr_assert(func_expr);

  FilterXObject *obj = init_and_eval_expr(func_expr);
  cr_assert(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(null)));

  filterx_object_unref(obj);
  filterx_expr_unref(func_expr);
}

Test(filterx_datetime, test_filterx_datetime_strptime_invalid_arg_type)
{
  const gchar *test_time_str = "2024-04-08T10:11:12Z";
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(test_time_str, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_integer_new(1337))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(datefmt_isodate,
                                                      -1))));

  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(args, NULL), NULL);
  cr_assert_null(func_expr);
}


Test(filterx_datetime, test_filterx_datetime_strptime_with_non_literal_format)
{
  FilterXObject *list = filterx_test_list_new();
  FilterXObject *format = filterx_string_new(datefmt_isodate, -1);
  cr_assert(filterx_sequence_append(list, &format));
  filterx_object_unref(format);
  FilterXExpr *format_expr = filterx_get_subscript_new(filterx_literal_new(list),
                                                       filterx_literal_new(filterx_integer_new(-1)));

  const gchar *test_time_str = "2024-04-08T10:11:12Z";
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(test_time_str, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, format_expr));

  FilterXExpr *func_expr = filterx_function_strptime_new(filterx_function_args_new(args, NULL), NULL);
  cr_assert_null(func_expr, "%p", func_expr);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_datetime, .init = setup, .fini = teardown);

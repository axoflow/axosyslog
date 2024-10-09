/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 */

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "apphook.h"
#include "scratch-buffers.h"

#include "filterx-func-parse-leef.h"
#include "event-format-parser.h"

#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/expr-literal.h"
#include "filterx/object-json.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"


FilterXExpr *
_new_leef_parser(FilterXFunctionArgs *args, GError **error, FilterXObject *fillable)
{
  FilterXExpr *func = filterx_function_parse_leef_new(args, error);

  if (!func)
    return NULL;

  FilterXExpr *fillable_expr = filterx_non_literal_new(fillable);
  filterx_generator_set_fillable(func, fillable_expr);

  return func;
}

FilterXFunctionArgs *
_assert_create_args(const gchar *input)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(input, -1))));
  GError *args_err = NULL;
  FilterXFunctionArgs *result = filterx_function_args_new(args, &args_err);
  cr_assert_null(args_err);
  g_error_free(args_err);
  return result;
}

FilterXObject *
_eval_leef_input(const gchar *input, GError **error)
{
  FilterXExpr *func = _new_leef_parser(_assert_create_args(input), error, filterx_json_object_new_empty());
  FilterXObject *obj = filterx_expr_eval(func);
  filterx_expr_unref(func);
  return obj;
}

void
_assert_leef_parser_result(const gchar *input, const gchar *expected_result)
{

  GError *err = NULL;
  FilterXObject *obj = _eval_leef_input(input, &err);
  cr_assert_null(err);
  cr_assert_not_null(obj);

  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(dict)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, expected_result);

  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_leef, test_invalid_input)
{
  GError *error = NULL;
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_integer_new(33))));
  GError *args_err = NULL;
  FilterXFunctionArgs *fx_args = filterx_function_args_new(args, &args_err);
  cr_assert_null(args_err);
  g_error_free(args_err);

  FilterXExpr *func = _new_leef_parser(fx_args, &error, filterx_json_object_new_empty());
  cr_assert_null(error);
  FilterXObject *obj = filterx_expr_eval(func);
  cr_assert_null(obj);

  cr_assert_str_eq(EVENT_FORMAT_PARSER_ERR_NOT_STRING_INPUT_MSG, filterx_eval_get_last_error());

  filterx_expr_unref(func);
  g_error_free(error);
}

Test(filterx_func_parse_leef, test_invalid_version)
{
  GError *init_err = NULL;
  cr_assert_null(
    _eval_leef_input("INVALID|Microsoft|MSExchange|4.0 SP1|15345|src=192.0.2.0 dst=172.50.123.1 sev=5cat=anomaly srcPort=81 dstPort=21 usrName=joe.black",
                     &init_err));
  cr_assert_null(init_err);
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_not_null(last_error);
  GString *expected_err_msg = scratch_buffers_alloc();
  g_string_append_printf(expected_err_msg, EVENT_FORMAT_PARSER_ERR_NO_LOG_SIGN_MSG, "LEEF");
  cr_assert_str_eq(expected_err_msg->str, last_error);
}

Test(filterx_func_parse_leef, test_invalid_log_signature)
{
  GError *init_err = NULL;
  cr_assert_null(
    _eval_leef_input("BAD_SIGN:1.0|Microsoft|MSExchange|4.0 SP1|15345|src=192.0.2.0 dst=172.50.123.1 sev=5cat=anomaly srcPort=81 dstPort=21 usrName=joe.black",
                     &init_err));
  cr_assert_null(init_err);
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_not_null(last_error);
  GString *expected_err_msg = scratch_buffers_alloc();
  g_string_append_printf(expected_err_msg, EVENT_FORMAT_PARSER_ERR_LOG_SIGN_DIFFERS_MSG, "BAD_SIGN:1.0", "LEEF");
  cr_assert_str_eq(expected_err_msg->str, last_error);
}

Test(filterx_func_parse_leef, test_header_missing_field)
{
  GError *init_err = NULL;
  cr_assert_null(_eval_leef_input("LEEF:1.0|Microsoft|MSExchange", &init_err));
  cr_assert_null(init_err);
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_not_null(last_error);
  GString *expected_err_msg = scratch_buffers_alloc();
  g_string_append_printf(expected_err_msg, EVENT_FORMAT_PARSER_ERR_MISSING_COLUMNS_MSG, (guint64)3, (guint64)6);
  cr_assert_str_eq(expected_err_msg->str, last_error);
}

Test(filterx_func_parse_leef, test_basic_leef_message)
{
  _assert_leef_parser_result("LEEF:1.0|Microsoft|MSExchange|4.0 SP1|15345|src=192.0.2.0 dst=172.50.123.1 sev=5cat=anomaly srcPort=81 dstPort=21 usrName=joe.black",
                             "{\"version\":\"1.0\",\"vendor\":\"Microsoft\",\"product_name\":\"MSExchange\",\"product_version\":\"4.0 SP1\",\"event_id\":\"15345\",\"extensions\":{\"src\":\"192.0.2.0\",\"dst\":\"172.50.123.1\",\"sev\":\"5cat=anomaly\",\"srcPort\":\"81\",\"dstPort\":\"21\",\"usrName\":\"joe.black\"}}");
}

Test(filterx_func_parse_leef, test_extensions_empty)
{
  _assert_leef_parser_result("LEEF:1.0|Microsoft|MSExchange|4.0 SP1|15345|",
                             "{\"version\":\"1.0\",\"vendor\":\"Microsoft\",\"product_name\":\"MSExchange\",\"product_version\":\"4.0 SP1\",\"event_id\":\"15345\",\"extensions\":{}}");
}

Test(filterx_func_parse_leef, test_header_escaped_delimiter)
{
  _assert_leef_parser_result("LEEF:1.0|Micro\\|soft|MSExchange|4.0 SP1|15345|src=192.0.2.0 dst=172.50.123.1 sev=5cat=anomaly srcPort=81 dstPort=21 usrName=joe.black",
                             "{\"version\":\"1.0\",\"vendor\":\"Micro|soft\",\"product_name\":\"MSExchange\",\"product_version\":\"4.0 SP1\",\"event_id\":\"15345\",\"extensions\":{\"src\":\"192.0.2.0\",\"dst\":\"172.50.123.1\",\"sev\":\"5cat=anomaly\",\"srcPort\":\"81\",\"dstPort\":\"21\",\"usrName\":\"joe.black\"}}");
}


Test(filterx_func_parse_leef, test_extension_escaped_delimiter)
{
  _assert_leef_parser_result("LEEF:1.0|Microsoft|MSExchange|4.0 SP1|15345|foo=foo\\=bar\\=baz tik=tik\\=tak\\=toe",
                             "{\"version\":\"1.0\",\"vendor\":\"Microsoft\",\"product_name\":\"MSExchange\",\"product_version\":\"4.0 SP1\",\"event_id\":\"15345\",\"extensions\":{\"foo\":\"foo=bar=baz\",\"tik\":\"tik=tak=toe\"}}");
}

Test(filterx_func_parse_leef, test_header_do_not_strip_whitespaces)
{
  _assert_leef_parser_result("LEEF:1.0| Microsoft |  MSExchange  |   4.0 SP1   |    15345    |",
                             "{\"version\":\"1.0\",\"vendor\":\" Microsoft \",\"product_name\":\"  MSExchange  \",\"product_version\":\"   4.0 SP1   \",\"event_id\":\"    15345    \",\"extensions\":{}}");
}

Test(filterx_func_parse_leef, test_extensions_space_in_value)
{
  _assert_leef_parser_result("LEEF:1.0|Microsoft|MSExchange|4.0 SP1|15345|foo=bar baz tik=tak toe",
                             "{\"version\":\"1.0\",\"vendor\":\"Microsoft\",\"product_name\":\"MSExchange\",\"product_version\":\"4.0 SP1\",\"event_id\":\"15345\",\"extensions\":{\"foo\":\"bar baz\",\"tik\":\"tak toe\"}}");
}

// TODO: fix spaces?
// Test(filterx_func_parse_cef, test_extensions_trailing_space_in_key)
// {
//   _assert_cef_parser_result("CEF:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|foo bar=bar baz",
//   "{\"version\":\"0\",\"deviceVendor\":\"KasperskyLab\",\"deviceProduct\":\"SecurityCenter\",\"deviceVersion\":\"13.2.0.1511\",\"deviceEventClassId\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agentSeverity\":\"1\",\"extensions\":{\"foo bar\":\"bar baz\"}}");
// }

// Test(filterx_func_parse_cef, test_extensions_trailing_space_in_value)
// {
//   _assert_cef_parser_result("CEF:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|foo= bar baz tik=  tak toe ",
//   "{\"version\":\"0\",\"deviceVendor\":\"KasperskyLab\",\"deviceProduct\":\"SecurityCenter\",\"deviceVersion\":\"13.2.0.1511\",\"deviceEventClassId\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agentSeverity\":\"1\",\"extensions\":{\"foo\":\"bar baz\"}}");
// }

Test(filterx_func_parse_leef, test_header_whitespaces)
{
  _assert_leef_parser_result("LEEF:1.0|Mic roso  ft|MSExchange|4.0 SP1|15345|",
                             "{\"version\":\"1.0\",\"vendor\":\"Mic roso  ft\",\"product_name\":\"MSExchange\",\"product_version\":\"4.0 SP1\",\"event_id\":\"15345\",\"extensions\":{}}");
}

Test(filterx_func_parse_leef, test_header_leading_trailing_whitespaces)
{
  _assert_leef_parser_result("LEEF:1.0|  Microsoft |MSExchange|4.0 SP1|15345|foo=foo\\=bar\\=baz tik=tik\\=tak\\=toe",
                             "{\"version\":\"1.0\",\"vendor\":\"  Microsoft \",\"product_name\":\"MSExchange\",\"product_version\":\"4.0 SP1\",\"event_id\":\"15345\",\"extensions\":{\"foo\":\"foo=bar=baz\",\"tik\":\"tik=tak=toe\"}}");
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

TestSuite(filterx_func_parse_leef, .init = setup, .fini = teardown);

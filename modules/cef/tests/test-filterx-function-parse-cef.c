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

#include "filterx-func-parse-cef.h"
#include "event-format-parser.h"

#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/expr-literal.h"
#include "filterx/object-json.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"

#include "test_helpers.h"

Test(filterx_func_parse_cef, test_invalid_input)
{
  GError *error = NULL;
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_integer_new(33))));
  GError *args_err = NULL;
  FilterXFunctionArgs *fx_args = filterx_function_args_new(args, &args_err);
  cr_assert_null(args_err);
  g_error_free(args_err);

  FilterXExpr *func = _new_parser(fx_args, &error, filterx_json_object_new_empty());
  cr_assert_null(error);
  FilterXObject *obj = filterx_expr_eval(func);
  cr_assert_null(obj);

  cr_assert_str_eq(EVENT_FORMAT_PARSER_ERR_NOT_STRING_INPUT_MSG, filterx_eval_get_last_error());

  filterx_expr_unref(func);
  g_error_free(error);
}

Test(filterx_func_parse_cef, test_invalid_version)
{
  const gchar *input =
    "INVALID|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|rt=1647626887000 cs9=site location Bldg cs9Label=GroupName dhost=WS6465 dst=10.55.203.12 cs2=KES cs2Label=ProductName cs3=11.0.0.0 cs3Label=ProductVersion cs10=Uninstall EDR cs10Label=TaskName cs4=885 cs4Label=TaskId cn2=4 cn2Label=TaskNewState cn1=0 cn1Label=TaskOldState";
  GError *init_err = NULL;
  cr_assert_null(_eval_input(&init_err, _create_msg_arg(input), NULL));
  cr_assert_null(init_err);
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_not_null(last_error);
  GString *expected_err_msg = scratch_buffers_alloc();
  g_string_append_printf(expected_err_msg, EVENT_FORMAT_PARSER_ERR_NO_LOG_SIGN_MSG, "CEF");
  cr_assert_str_eq(expected_err_msg->str, last_error);
}

Test(filterx_func_parse_cef, test_invalid_log_signature)
{
  const gchar *input =
    "BAD_SIGN:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|rt=1647626887000 cs9=site location Bldg cs9Label=GroupName dhost=WS6465 dst=10.55.203.12 cs2=KES cs2Label=ProductName cs3=11.0.0.0 cs3Label=ProductVersion cs10=Uninstall EDR cs10Label=TaskName cs4=885 cs4Label=TaskId cn2=4 cn2Label=TaskNewState cn1=0 cn1Label=TaskOldState";
  GError *init_err = NULL;
  cr_assert_null(_eval_input(&init_err, _create_msg_arg(input), NULL));
  cr_assert_null(init_err);
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_not_null(last_error);
  GString *expected_err_msg = scratch_buffers_alloc();
  g_string_append_printf(expected_err_msg, EVENT_FORMAT_PARSER_ERR_LOG_SIGN_DIFFERS_MSG, "BAD_SIGN:0", "CEF");
  cr_assert_str_eq(expected_err_msg->str, last_error);
}

Test(filterx_func_parse_cef, test_header_missing_field)
{
  const gchar *input = "CEF:0|KasperskyLab|SecurityCenter|";
  GError *init_err = NULL;
  cr_assert_null(_eval_input(&init_err, _create_msg_arg(input), NULL));
  cr_assert_null(init_err);
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_not_null(last_error);
  GString *expected_err_msg = scratch_buffers_alloc();
  g_string_append_printf(expected_err_msg, EVENT_FORMAT_PARSER_ERR_MISSING_COLUMNS_MSG, (guint64)3, (guint64)8);
  cr_assert_str_eq(expected_err_msg->str, last_error);
}

Test(filterx_func_parse_cef, test_basic_cef_message)
{
  _assert_parser_result("CEF:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|rt=1647626887000 cs9=site location Bldg cs9Label=GroupName dhost=WS6465 dst=10.55.203.12 cs2=KES cs2Label=ProductName cs3=11.0.0.0 cs3Label=ProductVersion cs10=Uninstall EDR cs10Label=TaskName cs4=885 cs4Label=TaskId cn2=4 cn2Label=TaskNewState cn1=0 cn1Label=TaskOldState",
                        "{\"version\":\"0\",\"device_vendor\":\"KasperskyLab\",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{\"rt\":\"1647626887000\",\"cs9\":\"site location Bldg\",\"cs9Label\":\"GroupName\",\"dhost\":\"WS6465\",\"dst\":\"10.55.203.12\",\"cs2\":\"KES\",\"cs2Label\":\"ProductName\",\"cs3\":\"11.0.0.0\",\"cs3Label\":\"ProductVersion\",\"cs10\":\"Uninstall EDR\",\"cs10Label\":\"TaskName\",\"cs4\":\"885\",\"cs4Label\":\"TaskId\",\"cn2\":\"4\",\"cn2Label\":\"TaskNewState\",\"cn1\":\"0\",\"cn1Label\":\"TaskOldState\"}}");
}

Test(filterx_func_parse_cef, test_extensions_empty)
{
  _assert_parser_result("CEF:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|",
                        "{\"version\":\"0\",\"device_vendor\":\"KasperskyLab\",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{}}");
}

Test(filterx_func_parse_cef, test_header_escaped_delimiter)
{
  _assert_parser_result("CEF:0|Kaspers\\|kyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|rt=1647626887000",
                        "{\"version\":\"0\",\"device_vendor\":\"Kaspers|kyLab\",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{\"rt\":\"1647626887000\"}}");
}


Test(filterx_func_parse_cef, test_exteansion_escaped_delimiter)
{
  _assert_parser_result("CEF:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|escaped=foo\\=bar\\=baz",
                        "{\"version\":\"0\",\"device_vendor\":\"KasperskyLab\",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{\"escaped\":\"foo=bar=baz\"}}");
}

Test(filterx_func_parse_cef, test_header_do_not_strip_whitespaces)
{
  _assert_parser_result("CEF:0| KasperskyLab |  SecurityCenter  |   13.2.0.1511   |    KLPRCI_TaskState    |     Completed successfully     |      1      |",
                        "{\"version\":\"0\",\"device_vendor\":\" KasperskyLab \",\"device_product\":\"  SecurityCenter  \",\"device_version\":\"   13.2.0.1511   \",\"device_event_class_id\":\"    KLPRCI_TaskState    \",\"name\":\"     Completed successfully     \",\"agent_severity\":\"      1      \",\"extensions\":{}}");
}

Test(filterx_func_parse_cef, test_extensions_space_in_value)
{
  _assert_parser_result("CEF:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|foo=bar baz tik=tak toe",
                        "{\"version\":\"0\",\"device_vendor\":\"KasperskyLab\",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{\"foo\":\"bar baz\",\"tik\":\"tak toe\"}}");
}

Test(filterx_func_parse_cef, test_header_whitespaces)
{
  _assert_parser_result("CEF:0|Kasper sky  Lab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|",
                        "{\"version\":\"0\",\"device_vendor\":\"Kasper sky  Lab\",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{}}");
}

Test(filterx_func_parse_cef, test_header_leading_trailing_whitespaces)
{
  _assert_parser_result("CEF:0|  KasperskyLab |SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|",
                        "{\"version\":\"0\",\"device_vendor\":\"  KasperskyLab \",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{}}");
}

Test(filterx_func_parse_cef, test_forced_pair_separator)
{
  const gchar *input =
    "CEF:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|foo=bar@bar=baz@baz=tik\\=tak";
  _assert_parser_result_inner("{\"version\":\"0\",\"device_vendor\":\"KasperskyLab\",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{\"foo\":\"bar\",\"bar\":\"baz\",\"baz\":\"tik=tak\"}}",
                              _create_msg_arg(input), _create_pair_separator_arg("@"), NULL);
}

Test(filterx_func_parse_cef, test_forced_value_separator)
{
  const gchar *input =
    "CEF:0|KasperskyLab|SecurityCenter|13.2.0.1511|KLPRCI_TaskState|Completed successfully|1|foo#bar bar#baz baz#tik\\#tak";
  _assert_parser_result_inner("{\"version\":\"0\",\"device_vendor\":\"KasperskyLab\",\"device_product\":\"SecurityCenter\",\"device_version\":\"13.2.0.1511\",\"device_event_class_id\":\"KLPRCI_TaskState\",\"name\":\"Completed successfully\",\"agent_severity\":\"1\",\"extensions\":{\"foo\":\"bar\",\"bar\":\"baz\",\"baz\":\"tik#tak\"}}",
                              _create_msg_arg(input), _create_value_separator_arg("#"), NULL);
}

Test(filterx_func_parse_cef, test_forced_empty_value_separator)
{
  const gchar *input = "CEF:0|Microsoft|MSExchange|4.0 SP1|15345|foo#bar bar#baz baz#tik\\#tak";
  GError *error = NULL;
  FilterXExpr *func = _new_parser(_assert_create_args(0, _create_msg_arg(input), _create_value_separator_arg(""), NULL),
                                  &error, filterx_json_object_new_empty());
  cr_assert_not_null(error);
  cr_assert_null(func);

  GString *expected_err_msg = scratch_buffers_alloc();
  g_string_append_printf(expected_err_msg, EVENT_FORMAT_PARSER_ERR_EMPTY_STRING" "FILTERX_FUNC_PARSE_CEF_USAGE,
                         EVENT_FORMAT_PARSER_ARG_NAME_VALUE_SEPARATOR);

  cr_assert_str_eq(error->message, expected_err_msg->str);

  filterx_expr_unref(func);
  g_error_free(error);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
  set_constructor(filterx_function_parse_cef_new);
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_func_parse_cef, .init = setup, .fini = teardown);

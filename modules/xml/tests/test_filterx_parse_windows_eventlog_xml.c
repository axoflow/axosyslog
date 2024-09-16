/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx-parse-windows-eventlog-xml.h"
#include "filterx/object-string.h"
#include "filterx/object-json.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"
#include "apphook.h"
#include "cfg.h"

#include "libtest/filterx-lib.h"
#include <string.h>

static FilterXExpr *
_create_expr(const gchar *raw_xml, FilterXObject *fillable)
{
  FilterXFunctionArg *input = filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new(raw_xml, -1)));
  GList *args_list = g_list_append(NULL, input);
  GError *error = NULL;
  FilterXFunctionArgs *args = filterx_function_args_new(args_list, &error);
  g_assert(!error);

  FilterXExpr *func = filterx_generator_function_parse_windows_eventlog_xml_new(args, &error);
  g_assert(!error);

  FilterXExpr *fillable_expr = filterx_non_literal_new(fillable);
  filterx_generator_set_fillable(func, fillable_expr);

  g_error_free(error);
  return func;
}

static const gchar *
_create_input_from_event_data(const gchar *event_data_xml)
{
  GString *xml = scratch_buffers_alloc();
  g_string_printf(xml,
                  "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>\n"
                  "    <System>\n"
                  "        <Provider Name='EventCreate'/>\n"
                  "        <EventID Qualifiers='0'>999</EventID>\n"
                  "        <Version>0</Version>\n"
                  "        <Level>2</Level>\n"
                  "        <Task>0</Task>\n"
                  "        <Opcode>0</Opcode>\n"
                  "        <Keywords>0x80000000000000</Keywords>\n"
                  "        <TimeCreated SystemTime='2024-01-12T09:30:12.1566754Z'/>\n"
                  "        <EventRecordID>934</EventRecordID>\n"
                  "        <Correlation/>\n"
                  "        <Execution ProcessID='0' ThreadID='0'/>\n"
                  "        <Channel>Application</Channel>\n"
                  "        <Computer>DESKTOP-2MBFIV7</Computer>\n"
                  "        <Security UserID='S-1-5-21-3714454296-2738353472-899133108-1001'/>\n"
                  "    </System>\n"
                  "    <RenderingInfo Culture='en-US'>\n"
                  "        <Message>foobar</Message>\n"
                  "        <Level>Error</Level>\n"
                  "        <Task></Task>\n"
                  "        <Opcode>Info</Opcode>\n"
                  "        <Channel></Channel>\n"
                  "        <Provider></Provider>\n"
                  "        <Keywords>\n"
                  "            <Keyword>Classic</Keyword>\n"
                  "        </Keywords>\n"
                  "    </RenderingInfo>\n"
                  "    <EventData>\n"
                  "        %s\n"
                  "    </EventData>\n"
                  "</Event>",
                  event_data_xml);
  return xml->str;
}

static void
_assert_parse_event_data(const gchar *event_data_xml, const gchar *expected_eventdata_json)
{
  FilterXExpr *func = _create_expr(_create_input_from_event_data(event_data_xml), filterx_json_object_new_empty());

  FilterXObject *result = filterx_expr_eval(func);
  cr_assert(result);
  cr_assert(!filterx_eval_get_last_error());

  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(json_object)));

  GString *formatted_result = g_string_new(NULL);
  filterx_object_repr(result, formatted_result);

  const gchar *prefix = "{\"Event\":{\"@xmlns\":\"http:\\/\\/schemas.microsoft.com\\/win\\/2004\\/08\\/events\\/event\""
                        ",\"System\":{\"Provider\":{\"@Name\":\"EventCreate\"},\"EventID\":{\"@Qualifiers\":\"0\",\"#te"
                        "xt\":\"999\"},\"Version\":\"0\",\"Level\":\"2\",\"Task\":\"0\",\"Opcode\":\"0\",\"Keywords\":"
                        "\"0x80000000000000\",\"TimeCreated\":{\"@SystemTime\":\"2024-01-12T09:30:12.1566754Z\"},\"Even"
                        "tRecordID\":\"934\",\"Correlation\":\"\",\"Execution\":{\"@ProcessID\":\"0\",\"@ThreadID\":\"0"
                        "\"},\"Channel\":\"Application\",\"Computer\":\"DESKTOP-2MBFIV7\",\"Security\":{\"@UserID\":\"S"
                        "-1-5-21-3714454296-2738353472-899133108-1001\"}},\"RenderingInfo\":{\"@Culture\":\"en-US\",\"M"
                        "essage\":\"foobar\",\"Level\":\"Error\",\"Task\":\"\",\"Opcode\":\"Info\",\"Channel\":\"\",\"P"
                        "rovider\":\"\",\"Keywords\":{\"Keyword\":\"Classic\"}},\"EventData\":";
  const gchar *suffix = "}}";

  cr_assert_eq(memcmp(formatted_result->str, prefix, strlen(prefix)), 0);

  /* Needed for sensible assertion error reporting. */
  GString *formatted_eventdata = g_string_new(formatted_result->str + strlen(prefix));
  g_string_truncate(formatted_eventdata, formatted_eventdata->len - strlen(suffix));
  cr_assert_str_eq(formatted_eventdata->str, expected_eventdata_json);

  cr_assert_eq(memcmp(formatted_result->str + strlen(prefix) + strlen(expected_eventdata_json),
                      suffix, strlen(suffix)), 0);

  g_string_free(formatted_eventdata, TRUE);
  g_string_free(formatted_result, TRUE);
  filterx_object_unref(result);
  filterx_expr_unref(func);
}

static void
_assert_parse_fail(const gchar *xml)
{
  FilterXExpr *func = _create_expr(xml, filterx_json_object_new_empty());

  FilterXObject *result = filterx_expr_eval(func);
  cr_assert(!result);
  cr_assert(filterx_eval_get_last_error());

  filterx_eval_clear_errors();
  filterx_expr_unref(func);
}

static void
_assert_parse_event_data_fail(const gchar *event_data_xml)
{
  _assert_parse_fail(_create_input_from_event_data(event_data_xml));
}

Test(filterx_parse_windows_eventlog_xml, valid_inputs)
{
  _assert_parse_event_data("<Data Name='param1'>foo</Data>\n",
                           "{\"Data\":{\"param1\":\"foo\"}}");

  _assert_parse_event_data("<Data Name='param1'>foo</Data>\n"
                           "<Data Name='param2'>bar</Data>\n",
                           "{\"Data\":{\"param1\":\"foo\",\"param2\":\"bar\"}}");

  _assert_parse_event_data("<Data>foo</Data>\n",
                           "{\"Data\":\"foo\"}");

  _assert_parse_event_data("<Data>foo</Data>\n"
                           "<Data>bar</Data>\n",
                           "{\"Data\":[\"foo\",\"bar\"]}");
}

Test(filterx_parse_windows_eventlog_xml, invalid_inputs)
{
  _assert_parse_event_data_fail("<Data almafa='param1'>foo</Data>\n");
  _assert_parse_event_data_fail("<Data Name='param1' almafa='kortefa'>foo</Data>\n");
}

static void
setup(void)
{
  configuration = cfg_new_snippet();
  app_startup();
  init_libtest_filterx();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
  cfg_free(configuration);
}

TestSuite(filterx_parse_windows_eventlog_xml, .init = setup, .fini = teardown);

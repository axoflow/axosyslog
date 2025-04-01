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

#include "filterx-parse-xml.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/object-dict-interface.h"
#include "filterx/json-repr.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"
#include "apphook.h"
#include "cfg.h"

#include "libtest/filterx-lib.h"

static FilterXExpr *
_create_parse_xml_expr(const gchar *raw_xml, FilterXObject *fillable)
{
  FilterXFunctionArg *input = filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new(raw_xml, -1)));
  GList *args_list = g_list_append(NULL, input);
  GError *error = NULL;
  FilterXFunctionArgs *args = filterx_function_args_new(args_list, &error);
  g_assert(!error);

  FilterXExpr *func = filterx_generator_function_parse_xml_new(args, &error);
  g_assert(!error);

  FilterXExpr *fillable_expr = filterx_non_literal_new(fillable);
  filterx_generator_set_fillable(func, fillable_expr);

  g_error_free(error);
  return func;
}

static void
_assert_parse_xml_fail(const gchar *raw_xml)
{
  FilterXExpr *func = _create_parse_xml_expr(raw_xml, filterx_dict_new());

  FilterXObject *result = filterx_expr_eval(func);
  cr_assert(!result);
  cr_assert(filterx_eval_get_last_error());

  filterx_eval_clear_errors();
  filterx_expr_unref(func);
}

static void
_assert_parse_xml_with_fillable(const gchar *raw_xml, const gchar *expected_json, FilterXObject *fillable)
{
  FilterXExpr *func = _create_parse_xml_expr(raw_xml, fillable);

  FilterXObject *result = filterx_expr_eval(func);
  cr_assert(result);
  cr_assert(!filterx_eval_get_last_error());

  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));

  GString *formatted_result = g_string_new(NULL);
  filterx_object_repr(result, formatted_result);
  cr_assert_str_eq(formatted_result->str, expected_json);

  g_string_free(formatted_result, TRUE);
  filterx_object_unref(result);
  filterx_expr_unref(func);
}

static void
_assert_parse_xml(const gchar *raw_xml, const gchar *expected_json)
{
  _assert_parse_xml_with_fillable(raw_xml, expected_json, filterx_dict_new());
}

Test(filterx_parse_xml, invalid_inputs)
{
  _assert_parse_xml_fail("");
  _assert_parse_xml_fail("simple string");
  _assert_parse_xml_fail("<tag></missingtag>");
  _assert_parse_xml_fail("<tag></tag></extraclosetag>");
  _assert_parse_xml_fail("<tag><tag></tag>");
  _assert_parse_xml_fail("<tag1><tag2>closewrongorder</tag1></tag2>");
  _assert_parse_xml_fail("<tag id=\"missingquote></tag>");
  _assert_parse_xml_fail("<tag id='missingquote></tag>");
  _assert_parse_xml_fail("<tag id=missingquote\"></tag>");
  _assert_parse_xml_fail("<tag id=missingquote'></tag>");
  _assert_parse_xml_fail("<space in tag/>");
  _assert_parse_xml_fail("</>");
  _assert_parse_xml_fail("<tag></tag>>");
}

Test(filterx_parse_xml, valid_inputs)
{
  _assert_parse_xml("<a></a>",
                    "{\"a\":\"\"}");
  _assert_parse_xml("<a><b></b></a>",
                    "{\"a\":{\"b\":\"\"}}");
  _assert_parse_xml("<a><b>foo</b></a>",
                    "{\"a\":{\"b\":\"foo\"}}");
  _assert_parse_xml("<a><b>foo</b><c>bar</c></a>",
                    "{\"a\":{\"b\":\"foo\",\"c\":\"bar\"}}");
  _assert_parse_xml("<a attr=\"attr_val\">foo</a>",
                    "{\"a\":{\"@attr\":\"attr_val\",\"#text\":\"foo\"}}");
  _assert_parse_xml("<a attr=\"attr_val\"></a>",
                    "{\"a\":{\"@attr\":\"attr_val\"}}");
  _assert_parse_xml("<a><b>c</b><b>d</b></a>",
                    "{\"a\":{\"b\":[\"c\",\"d\"]}}");
  _assert_parse_xml("<a><b>c</b><b>d</b><b>e</b></a>",
                    "{\"a\":{\"b\":[\"c\",\"d\",\"e\"]}}");
  _assert_parse_xml("<a><b attr=\"attr_val\">c</b><b>e</b></a>",
                    "{\"a\":{\"b\":[{\"@attr\":\"attr_val\",\"#text\":\"c\"},\"e\"]}}");
  _assert_parse_xml("<a><b>c</b><b attr=\"attr_val\">e</b></a>",
                    "{\"a\":{\"b\":[\"c\",{\"@attr\":\"attr_val\",\"#text\":\"e\"}]}}");
  _assert_parse_xml("<a><b>c</b><b>d</b><b><e>f</e></b></a>",
                    "{\"a\":{\"b\":[\"c\",\"d\",{\"e\":\"f\"}]}}");
  _assert_parse_xml("<a><b><c>d</c></b><b><e>f</e></b></a>",
                    "{\"a\":{\"b\":[{\"c\":\"d\"},{\"e\":\"f\"}]}}");
  _assert_parse_xml("<a><b><c>d</c></b><e>f</e><b><g>h</g></b></a>",
                    "{\"a\":{\"b\":[{\"c\":\"d\"},{\"g\":\"h\"}],\"e\":\"f\"}}");
  _assert_parse_xml("<a>b<c>d</c></a>",
                    "{\"a\":{\"#text\":\"b\",\"c\":\"d\"}}");
  _assert_parse_xml("<a>b<c></c>d</a>",
                    "{\"a\":{\"#text\":\"bd\",\"c\":\"\"}}");

  /*
   * This is not a valid XML, as a valid XML must have exactly one root element,
   * but supporting this could be useful for parsing sub-XMLs, also dropping these
   * XMLs would take additional processing/validating.
   */
  _assert_parse_xml("<a>b</a><a>c</a>",
                    "{\"a\":[\"b\",\"c\"]}");

}

Test(filterx_parse_xml, overwrite_existing_invalid_value)
{
  FilterXObject *fillable = filterx_object_from_json("{\"a\":42}", -1, NULL);
  _assert_parse_xml_with_fillable("<a><b>foo</b></a>", "{\"a\":{\"b\":\"foo\"}}", fillable);
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

TestSuite(filterx_parse_xml, .init = setup, .fini = teardown);

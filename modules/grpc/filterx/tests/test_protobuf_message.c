/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 shifter
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "func-protobuf-message.h"
#include "filterx/func-flatten.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/json-repr.h"
#include "filterx/expr-literal.h"
#include "filterx/func-keys.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "libtest/filterx-lib.h"

#include <criterion/criterion.h>

static const gchar *test_pb_msg_filepath = NULL;

static void
_helper_create_proto_file(const gchar *proto_content)
{
  FILE *f = fopen(test_pb_msg_filepath, "wb");
  cr_assert_not_null(f);

  size_t len = strlen(proto_content);

  size_t written = fwrite(proto_content, sizeof(gchar), len, f);
  cr_assert_eq(written, len);
  fclose(f);
}

static void
_assert_protobuf_message_init_fail(GList *args)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_protobuf_message_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert(!func);
  cr_assert(err);
  g_error_free(err);
  g_error_free(args_err);
}

static FilterXExpr *
_assert_protobuf_message_init_success(GList *args)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_protobuf_message_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert(!err);
  cr_assert(!args_err);
  cr_assert(func);
  g_error_free(err);
  g_error_free(args_err);
  return func;
}

Test(filterx_protobuf_message, invalid_args)
{
  GList *args = NULL;

  /* no args */
  _assert_protobuf_message_init_fail(NULL);

  /* non-literal recursive */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_test_dict_new())));
  _assert_protobuf_message_init_fail(args);
  args = NULL;

  /* no schema or schema_file */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_test_dict_new())));
  _assert_protobuf_message_init_fail(args);
  args = NULL;

  /* no schema not yet implemented */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_test_dict_new())));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA,
                                                      filterx_literal_new(filterx_test_dict_new())));
  _assert_protobuf_message_init_fail(args);
  args = NULL;

  /* wrong file name */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_test_dict_new())));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA_FILE,
                                                      filterx_literal_new(filterx_string_new("wrong.filename", -1))));
  _assert_protobuf_message_init_fail(args);
  args = NULL;

}

Test(filterx_protobuf_message, load_proto_from_file)
{
  test_pb_msg_filepath = "load_proto_from_file.proto";
  const gchar *test_proto =
    "syntax = \"proto3\";"
    "package test.pkg;"
    "message TestProto4 {"
    "string name = 1;"
    "}";

  _helper_create_proto_file(test_proto);

  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_test_dict_new())));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA_FILE,
                                                      filterx_literal_new(filterx_string_new(test_pb_msg_filepath, -1))));

  FilterXExpr *func = _assert_protobuf_message_init_success(args);

  filterx_expr_unref(func);
}

Test(filterx_protobuf_message, assign_vars)
{
  test_pb_msg_filepath = "assign_vars.proto";
  const gchar *test_proto =
    "syntax = \"proto3\";"
    "package test.pkg;"
    "message TestProto7 {"
    "  string name = 1;"
    "  int32 id = 2;"
    "  map<string, string> foobar = 3;"
    "  repeated string arr = 4;"
    "  nested sub = 5;"
    "  repeated nested repsub = 6;"
    "  message nested {"
    "    string foo = 1;"
    "    string bar = 2;"
    "    string baz = 3;"
    "  }"
    "}";

  _helper_create_proto_file(test_proto);

  GError *json_err = NULL;
  FilterXObject *dict = filterx_object_from_json(
                          "{\"name\": \"test message\","
                          "\"id\": 333,"
                          "\"foobar\": {"
                          "\"host\": \"test host\","
                          "\"time\": \"test time string\","
                          "\"date\": \"test date string\""
                          "},"
                          "\"arr\": [\"foo\", \"bar\", \"baz\", {\"map\": {\"more\": \"nested\"}}],"
                          "\"sub\": {\"foo\":\"FOO\", \"bar\":\"BAR\", \"baz\":\"BAZ\"},"
                          "\"repsub\": [{\"foo\": \"foo1\", \"bar\": \"bar1\", \"baz\": \"baz1\"},"
                          "{\"foo\": \"TIK\", \"bar\": \"TAK\", \"baz\": \"TOE\"}]"
                          "};"
                          , -1, &json_err);
  cr_assert_null(json_err);

  FilterXExpr *expr = filterx_non_literal_new(dict);

  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, expr));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA_FILE,
                                                      filterx_literal_new(filterx_string_new(test_pb_msg_filepath, -1))));

  FilterXExpr *func = _assert_protobuf_message_init_success(args);

  FilterXObject *result = filterx_expr_eval(func);
  cr_assert(result);

  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(protobuf)));

  filterx_object_unref(result);
  filterx_expr_unref(func);
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
  remove(test_pb_msg_filepath);
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_protobuf_message, .init = setup, .fini = teardown);

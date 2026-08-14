/*
 * Copyright (c) 2026 Axoflow
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

#include "filterx/filterx-parser.h"
#include "cfg.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "compat/json.h"

static gboolean
_compile(const gchar *script)
{
  CfgLexer *lexer = cfg_lexer_new_buffer(configuration, script, strlen(script));

  return filterx_compile_script(configuration, lexer, NULL);
}

static struct json_object *
_compile_ast(const gchar *script)
{
  CfgLexer *lexer = cfg_lexer_new_buffer(configuration, script, strlen(script));
  GString *ast = NULL;

  if (!filterx_compile_script(configuration, lexer, &ast))
    {
      cr_assert_null(ast, "no AST is expected for a script that failed to compile");
      return NULL;
    }

  cr_assert_not_null(ast);
  struct json_object *result = json_tokener_parse(ast->str);

  cr_assert_not_null(result, "the AST is not valid JSON: %s", ast->str);
  g_string_free(ast, TRUE);

  return result;
}

static struct json_object *
_get(struct json_object *object, const gchar *key)
{
  struct json_object *value = NULL;

  cr_assert(json_object_object_get_ex(object, key, &value), "missing key: %s", key);
  return value;
}

static const gchar *
_get_str(struct json_object *object, const gchar *key)
{
  return json_object_get_string(_get(object, key));
}

static struct json_object *
_get_children(struct json_object *node, gsize expected_length)
{
  struct json_object *children = _get(node, "children");

  cr_assert(json_object_is_type(children, json_type_array));
  cr_assert_eq(json_object_array_length(children), expected_length,
               "unexpected number of children: %s", json_object_to_json_string(children));
  return children;
}

static struct json_object *
_get_child(struct json_object *node, gsize index, gsize expected_length)
{
  return json_object_array_get_idx(_get_children(node, expected_length), index);
}

Test(filterx_script, empty_input_compiles_to_an_empty_block)
{
  cr_assert(_compile(""));
  cr_assert(_compile("\n\n# just a comment\n"));
}

Test(filterx_script, brace_less_statement_list_compiles)
{
  cr_assert(_compile("$MSG = \"foo\";\n"
                     "$MSG2 = $MSG + \"bar\";\n"));
}

Test(filterx_script, multi_statement_script_with_control_flow_compiles)
{
  cr_assert(_compile("$MSG = json({\"a\": 1});\n"
                     "if (isset($MSG.a)) {\n"
                     "  $MSG.b = $MSG.a + 1;\n"
                     "} else {\n"
                     "  $MSG.b = 0;\n"
                     "};\n"
                     "$LEN = len($MSG);\n"));
}

Test(filterx_script, trailing_statement_without_semicolon_is_rejected)
{
  cr_assert_not(_compile("$MSG = \"foo\"\n"));
}

Test(filterx_script, unterminated_conditional_is_rejected)
{
  cr_assert_not(_compile("if (isset($MSG.foo)) {\n"
                         "  $MSG.bar = 1;\n"));
}

Test(filterx_script, stray_closing_brace_is_rejected)
{
  cr_assert_not(_compile("$MSG = \"foo\";\n}\n"));
}

Test(filterx_script, unknown_function_is_rejected)
{
  cr_assert_not(_compile("$MSG = there_is_no_such_filterx_function();\n"));
}

Test(filterx_script, invalid_regexp_is_rejected)
{
  cr_assert_not(_compile("$MSG =~ /(/;\n"));
}

Test(filterx_script, ast_of_an_empty_script_is_an_empty_toplevel_block)
{
  struct json_object *ast = _compile_ast("\n# nothing to see here\n");

  cr_assert_str_eq(_get_str(ast, "type"), "compound");
  cr_assert_str_eq(_get_str(ast, "text"), "<toplevel>");
  _get_children(ast, 0);

  json_object_put(ast);
}

Test(filterx_script, ast_lists_the_toplevel_statements_as_children)
{
  struct json_object *ast = _compile_ast("$MSG = \"foo\";\n"
                                         "if (isset($MSG)) {\n"
                                         "  $MSG2 = $MSG;\n"
                                         "};\n");

  cr_assert_str_eq(_get_str(ast, "type"), "compound");
  _get_children(ast, 2);
  cr_assert_str_eq(_get_str(_get_child(ast, 0, 2), "type"), "assign");
  cr_assert_str_eq(_get_str(_get_child(ast, 1, 2), "type"), "conditional");

  json_object_put(ast);
}

Test(filterx_script, ast_nodes_carry_their_source_text_and_location)
{
  struct json_object *ast = _compile_ast("\n"
                                         "$MSG = \"foo\";\n");
  struct json_object *assign = _get_child(ast, 0, 1);
  struct json_object *location = _get(assign, "location");

  cr_assert_str_eq(_get_str(assign, "text"), "$MSG = \"foo\"");
  cr_assert_eq(json_object_get_int(_get(location, "line")), 2);
  cr_assert_gt(json_object_get_int(_get(location, "column")), 0);

  json_object_put(ast);
}

Test(filterx_script, ast_is_dumped_after_optimization)
{
  struct json_object *ast = _compile_ast("$MSG = 1 + 2;\n");
  struct json_object *assign = _get_child(ast, 0, 1);
  struct json_object *rhs = _get_child(assign, 1, 2);

  cr_assert_str_eq(_get_str(rhs, "type"), "literal");
  /* leaf expressions have no children of their own */
  _get_children(rhs, 0);

  json_object_put(ast);
}

Test(filterx_script, no_ast_is_produced_for_a_script_that_does_not_compile)
{
  cr_assert_null(_compile_ast("$MSG = there_is_no_such_filterx_function();\n"));
  cr_assert_null(_compile_ast("$MSG = \"foo\"\n"));
}

static void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  cfg_free(configuration);
  configuration = NULL;
  app_shutdown();
}

TestSuite(filterx_script, .init = setup, .fini = teardown);

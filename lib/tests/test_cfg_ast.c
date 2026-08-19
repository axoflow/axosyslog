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
#include "libtest/config_parse_lib.h"

#include "cfg.h"
#include "cfg-ast.h"
#include "cfg-grammar.h"
#include "apphook.h"
#include "compat/json.h"

static struct json_object *
_parse_ast(const gchar *config)
{
  cr_assert(parse_config(config, LL_CONTEXT_ROOT, NULL, NULL), "Parsing the configuration failed: %s", config);

  GString *ast = cfg_ast_format(configuration);
  struct json_object *result = json_tokener_parse(ast->str);

  cr_assert_not_null(result, "cfg_ast_format() did not produce valid JSON: %s", ast->str);
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
_get_idx(struct json_object *array, gsize index)
{
  cr_assert(json_object_is_type(array, json_type_array));
  cr_assert(index < json_object_array_length(array), "index %" G_GSIZE_FORMAT " is out of range", index);
  return json_object_array_get_idx(array, index);
}

static struct json_object *
_get_statements(struct json_object *ast, gsize expected_length)
{
  struct json_object *statements = _get(ast, "statements");

  cr_assert_eq(json_object_array_length(statements), expected_length,
               "unexpected number of statements: %s", json_object_to_json_string(statements));
  return statements;
}

Test(cfg_ast, root_statements_are_reported_in_source_order)
{
  /* the log statement is stored in the rules of the cfg-tree, the source in the
   * hash table of the named objects, still they are reported in source order */
  struct json_object *ast = _parse_ast("log { source(s_in); };\n"
                                       "source s_in { internal(); };\n");
  struct json_object *statements = _get_statements(ast, 2);

  cr_assert_str_eq(_get_str(_get_idx(statements, 0), "kind"), "log");
  cr_assert_str_eq(_get_str(_get_idx(statements, 1), "kind"), "source");
  cr_assert_str_eq(_get_str(_get(_get_idx(statements, 1), "expr"), "name"), "s_in");

  json_object_put(ast);
}

Test(cfg_ast, statements_that_are_not_log_expressions_are_not_reported)
{
  /* options{}, template{}, template-function and block{} definitions are not
   * part of the cfg-tree, so they are invisible in the AST */
  struct json_object *ast = _parse_ast("options { keep-hostname(yes); };\n"
                                       "template t_x { template(\"$MSG\\n\"); };\n"
                                       "template-function tf_x \"$MSG\";\n"
                                       "block source my_source() { internal(); };\n"
                                       "source s_in { internal(); };\n");
  struct json_object *statements = _get_statements(ast, 1);

  cr_assert_str_eq(_get_str(_get_idx(statements, 0), "kind"), "source");

  json_object_put(ast);
}

Test(cfg_ast, statements_carry_their_source_location)
{
  struct json_object *ast = _parse_ast("source s_in { internal(); };\n"
                                       "\n"
                                       "log { source(s_in); };\n");
  struct json_object *statements = _get_statements(ast, 2);
  struct json_object *location = _get(_get(_get_idx(statements, 1), "expr"), "location");

  cr_assert_eq(json_object_get_int(_get(location, "line")), 3);
  cr_assert_gt(json_object_get_int(_get(location, "column")), 0);

  json_object_put(ast);
}

Test(cfg_ast, named_objects_are_dumped_as_expression_trees)
{
  struct json_object *ast = _parse_ast("source s_in { internal(); };\n");
  struct json_object *expr = _get(_get_idx(_get_statements(ast, 1), 0), "expr");

  cr_assert_str_eq(_get_str(expr, "layout"), "sequence");
  cr_assert_str_eq(_get_str(expr, "content"), "source");
  cr_assert_str_eq(_get_str(expr, "name"), "s_in");

  /* source { ... } is compiled into a junction of the drivers listed inside */
  struct json_object *junction = _get_idx(_get(expr, "children"), 0);
  cr_assert_str_eq(_get_str(junction, "layout"), "junction");

  /* internal() is built into the grammar instead of being a plugin, so it has
   * no plugin name, but it is still a single pipe in the tree */
  struct json_object *driver = _get_idx(_get(junction, "children"), 0);
  cr_assert_str_eq(_get_str(driver, "layout"), "single");
  cr_assert(json_object_is_type(_get(driver, "plugin"), json_type_null));

  json_object_put(ast);
}

Test(cfg_ast, log_path_flags_are_dumped)
{
  struct json_object *ast = _parse_ast("source s_in { internal(); };\n"
                                       "log { source(s_in); flags(final, flow-control); };\n");
  struct json_object *expr = _get(_get_idx(_get_statements(ast, 2), 1), "expr");
  struct json_object *flags = _get(expr, "flags");

  cr_assert_eq(json_object_array_length(flags), 2);
  cr_assert_str_eq(json_object_get_string(_get_idx(flags, 0)), "final");
  cr_assert_str_eq(json_object_get_string(_get_idx(flags, 1)), "flow-control");

  json_object_put(ast);
}

Test(cfg_ast, filterx_blocks_are_dumped_as_filterx_expressions)
{
  struct json_object *ast = _parse_ast("source s_in { internal(); };\n"
                                       "log { source(s_in); filterx { $MSG = \"hello\"; }; };\n");
  struct json_object *log_expr = _get(_get_idx(_get_statements(ast, 2), 1), "expr");

  /* filterx {} is wrapped into a filter expression holding a single pipe */
  struct json_object *filter = _get_idx(_get(log_expr, "children"), 1);
  cr_assert_str_eq(_get_str(filter, "content"), "filter");

  struct json_object *pipe = _get_idx(_get(filter, "children"), 0);
  cr_assert_str_eq(_get_str(pipe, "layout"), "single");
  cr_assert_str_eq(_get_str(pipe, "plugin"), "filterx");

  struct json_object *filterx = _get(pipe, "filterx");
  cr_assert_str_eq(_get_str(filterx, "type"), "compound");

  struct json_object *stmt = _get_idx(_get(filterx, "children"), 0);
  cr_assert_str_eq(_get_str(stmt, "text"), "$MSG = \"hello\"");
  cr_assert_gt(json_object_get_int(_get(_get(stmt, "location"), "line")), 0);

  json_object_put(ast);
}

Test(cfg_ast, unsafe_characters_in_the_config_are_escaped)
{
  /* the JSON would be malformed if the quote or the backslash were not escaped,
   * _parse_ast() asserts on that */
  struct json_object *ast = _parse_ast("source \"s_\\\"quote\\\\\" { internal(); };\n");
  struct json_object *expr = _get(_get_idx(_get_statements(ast, 1), 0), "expr");

  cr_assert_str_eq(_get_str(expr, "name"), "s_\"quote\\");

  json_object_put(ast);
}

Test(cfg_ast, a_filterx_block_escaped_by_a_snippet_shows_up_as_extra_statements)
{
  /* the config below is what a generated configuration looks like once the
   * snippet embedded into its filterx block has closed the block and injected
   * statements of its own: the extra statements are visible in the AST */
  struct json_object *ast = _parse_ast("source s_in { internal(); };\n"
                                       "log { source(s_in); filterx {\n"
                                       "  $MSG = \"x\";\n"
                                       "}; };\n"
                                       "log { source(s_in); };\n");
  struct json_object *statements = _get_statements(ast, 3);

  cr_assert_str_eq(_get_str(_get_idx(statements, 2), "kind"), "log");

  json_object_put(ast);
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
  cfg_free(configuration);
  configuration = NULL;
  app_shutdown();
}

TestSuite(cfg_ast, .init = setup, .fini = teardown);

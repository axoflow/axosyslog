/*
 * Copyright (c) 2018 Balabit
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
#include <criterion/new/assert.h>
#include <string.h>

#include "cfg-lexer.h"
#include "cfg-args.h"
#include "cfg-block.h"
#include "cfg-parser.h"
#include "cfg.h"
#include "apphook.h"
#include "filter/filter-pipe.h"


CFG_LTYPE yyloc;
GString *result;

static void
setup(void)
{
  result = g_string_sized_new(100);

  yyloc.name = "config-file";
  yyloc.first_line = yyloc.first_column = 1;
  yyloc.last_line = yyloc.last_column = 1;

  configuration = cfg_new_snippet();
  msg_init(TRUE);
}

static void
teardown(void)
{
  msg_deinit();
  cfg_free(configuration);
  g_string_free(result, TRUE);
}

TestSuite(test_block, .init = setup, .fini = teardown);

Test(test_block, mandatory_arguments)
{
  CfgArgs *arg_defs = cfg_args_new();
  cfg_args_set(arg_defs, "mandatory-param", NULL);

  CfgBlockGenerator *generator = cfg_block_new(cfg_lexer_lookup_context_type_by_name("block"),
                                               "block_with_mandatory_options",
                                               "`mandatory_param`",
                                               arg_defs, &yyloc);

  CfgArgs *args = cfg_args_new();
  cr_assert(not(generator->generate(generator, configuration, args, result, "")), "mandatory parameter missing");

  cfg_args_set(args, "mandatory-param", "value");
  cr_assert(generator->generate(generator, configuration, args, result, ""));
  cr_assert(eq(str, result->str, "value"));

  cfg_args_unref(args);
  cfg_block_generator_unref(generator);
}

Test(test_block, varargs)
{
  CfgArgs *arg_defs = cfg_args_new();
  CfgBlockGenerator *generator = cfg_block_new(cfg_lexer_lookup_context_type_by_name("block"),
                                               "block_to_test_varargs",
                                               "`varargs_param`",
                                               arg_defs, &yyloc);

  CfgArgs *args = cfg_args_new();
  cfg_args_set(args, "varargs_param", "value");
  cr_assert(not(generator->generate(generator, configuration, args, result, "")), "varargs not set yet");

  cfg_args_accept_varargs(arg_defs);
  cr_assert(generator->generate(generator, configuration, args, result, ""));
  cr_assert(eq(str, result->str, "value"));

  cfg_args_unref(args);
  cfg_block_generator_unref(generator);
}

Test(test_block, positional_arguments_are_rejected)
{
  CfgArgs *arg_defs = cfg_args_new();
  cfg_args_accept_varargs(arg_defs);
  CfgBlockGenerator *generator = cfg_block_new(cfg_lexer_lookup_context_type_by_name("block"),
                                               "block_rejecting_positional_args",
                                               "`__VARARGS__`",
                                               arg_defs, &yyloc);

  CfgArgs *args = cfg_args_new();
  cfg_args_add_positional(args, "value");
  cr_assert(not(generator->generate(generator, configuration, args, result, "")), "positional args are rejected");

  cfg_args_unref(args);
  cfg_block_generator_unref(generator);
}

Test(test_block, varargs_preserve_explicit_empty_string)
{
  CfgArgs *arg_defs = cfg_args_new();
  cfg_args_accept_varargs(arg_defs);
  CfgBlockGenerator *generator = cfg_block_new(cfg_lexer_lookup_context_type_by_name("block"),
                                               "block_to_test_empty_vararg",
                                               "`__VARARGS__`",
                                               arg_defs, &yyloc);
  CfgArgs *args = cfg_args_new();
  cfg_args_set(args, "empty-param", "");

  cr_assert(generator->generate(generator, configuration, args, result, ""));
  cr_assert(eq(str, result->str, "empty_param(\"\") "));

  cfg_args_unref(args);
  cfg_block_generator_unref(generator);
}

Test(test_block, varargs_preserve_argument_without_value)
{
  CfgArgs *arg_defs = cfg_args_new();
  cfg_args_accept_varargs(arg_defs);
  CfgBlockGenerator *generator = cfg_block_new(cfg_lexer_lookup_context_type_by_name("block"),
                                               "block_to_test_valueless_vararg",
                                               "`__VARARGS__`",
                                               arg_defs, &yyloc);
  CfgArgs *args = cfg_args_new();
  cfg_args_set(args, "empty-param", NULL);

  cr_assert(generator->generate(generator, configuration, args, result, ""));
  cr_assert(eq(str, result->str, "empty_param() "));

  cfg_args_unref(args);
  cfg_block_generator_unref(generator);
}

static void
setup_config_parse(void)
{
  app_startup();
  configuration = cfg_new_snippet();
}

static void
teardown_config_parse(void)
{
  cfg_free(configuration);
  configuration = NULL;
  app_shutdown();
}

static gboolean
parse_block_config(const gchar *config)
{
  CfgLexer *lexer = cfg_lexer_new_buffer(configuration, config, strlen(config));
  gpointer parsed = NULL;
  return cfg_run_parser(configuration, lexer, &main_parser, &parsed, NULL);
}

static gboolean
parsed_filter_matches(void)
{
  LogExprNode *rule = cfg_tree_get_object(&configuration->tree, ENC_FILTER, "f");
  cr_assert(not(zero(ptr, rule)));
  cr_assert(not(zero(ptr, rule->children)));
  LogFilterPipe *filter = (LogFilterPipe *) rule->children->object;
  cr_assert(not(zero(ptr, filter)));
  cr_assert(filter_expr_init(filter->expr, configuration));

  LogMessage *message = log_msg_new_empty();
  gboolean matches = filter_expr_eval(filter->expr, message);
  log_msg_unref(message);
  return matches;
}

TestSuite(test_block_config, .init = setup_config_parse, .fini = teardown_config_parse);

Test(test_block_config, nested_varargs_forward_explicit_empty_string)
{
  cr_assert(parse_block_config(
              "block filter inner(value()) { \"prefix`value`suffix\" eq \"prefixsuffix\" };"
              "block filter outer(...) { inner(`__VARARGS__`) };"
              "filter f { outer(value(\"\")); };"));
  cr_assert(parsed_filter_matches());
}

Test(test_block_config, nested_varargs_empty_string_overrides_nonempty_default)
{
  cr_assert(parse_block_config(
              "block filter inner(value(\"default\")) { \"prefix`value`suffix\" eq \"prefixsuffix\" };"
              "block filter outer(...) { inner(`__VARARGS__`) };"
              "filter f { outer(value(\"\")); };"));
  cr_assert(parsed_filter_matches());
}

Test(test_block_config, nested_varargs_forward_nonempty_string)
{
  cr_assert(parse_block_config(
              "block filter inner(value()) { \"prefix`value`suffix\" eq \"prefixcontentsuffix\" };"
              "block filter outer(...) { inner(`__VARARGS__`) };"
              "filter f { outer(value(\"content\")); };"));
  cr_assert(parsed_filter_matches());
}

Test(test_block_config, nested_varargs_preserve_optional_empty_default)
{
  cr_assert(parse_block_config(
              "block filter inner(value(\"\")) { \"prefix`value`suffix\" eq \"prefixsuffix\" };"
              "block filter outer(...) { inner(`__VARARGS__`) };"
              "filter f { outer(); };"));
  cr_assert(parsed_filter_matches());
}

Test(test_block_config, nested_varargs_keep_valueless_mandatory_argument_missing)
{
  cr_assert(not(parse_block_config(
                  "block filter inner(value()) { \"prefix`value`suffix\" eq \"prefixsuffix\" };"
                  "block filter outer(...) { inner(`__VARARGS__`) };"
                  "filter f { outer(value()); };")));
}

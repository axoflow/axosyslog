/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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
#include "libtest/config_parse_lib.h"

#include "cfg.h"
#include "cfg-grammar.h"
#include "apphook.h"

static gboolean
_parse(const gchar *config)
{
  configuration = cfg_new_snippet();
  gboolean result = parse_config(config, LL_CONTEXT_ROOT, NULL, NULL);
  cfg_free(configuration);
  configuration = NULL;
  return result;
}

Test(cfg_parse_errors, syntax_error_at_the_end_of_an_in_memory_config_is_reported)
{
  cr_assert(not(_parse("log { filterx { x = (")));
}

Test(cfg_parse_errors, multi_line_error_location_past_the_line_end_is_reported)
{
  cr_assert(not(_parse("log { filterx "
                       "{ a =' d?\327\337? ;=\" \"\"bot0b\012; \012\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027 a =' d?\327\337? ;=\" \"; }"
                       " };")));
}

Test(cfg_parse_errors, invalid_regexp_in_a_match_sub_expression_is_a_parse_error)
{
  cr_assert(not(_parse("log { filterx { x = (\"a\" =~ \"(\"); }; };")));
}

/* LeakSanitizer fails the test if the parser leaks what it built before the error */
Test(cfg_parse_errors, syntax_error_frees_the_partially_built_filterx_block)
{
  const gchar *configs[] =
  {
    "log { filterx { x = 1; y = 2; ( }; };",
    "log { filterx { x = 1 + 2 ( }; };",
    "log { filterx { x = {\"a\": 1, \"b\": 2, ( }; };",
    "log { filterx { x = [1, 2, ( }; };",
    "log { filterx { x = (1, 2, ( }; };",
    "log { filterx { x = len(1, 2, ( }; };",
    "log { filterx { x = len(a=1, ( }; };",
    "log { filterx { dpath($x.a[1].b ( }; };",
    "log { filterx { if (x) { y = 1; } elif (z) { y = 2; } else ( }; };",
    "log { filterx { switch (x) { case 1: y = 1; case 2: ( }; };",
    "log { filterx { x = 1; ) }; };",
    "log { filterx { switch (x) { case 1: ) }; };",
    "log { filterx { x = {\"a\": 1, ; }; };",
    "log { filterx { x = [1, ; }; };",
    "log { filterx { x = (1, 2 ; }; };",
    "log { filterx { [a, b] = ; }; };",
    "log { filterx { (a, b) = ; }; };",
  };

  for (gint i = 0; i < G_N_ELEMENTS(configs); i++)
    cr_assert(not(_parse(configs[i])), "%s", configs[i]);
}

/* bison does not run the destructors of the rule whose action failed */
Test(cfg_parse_errors, failed_filterx_action_frees_what_it_owns)
{
  const gchar *configs[] =
  {
    "log { filterx { dpath($x) = 1; }; };",
    "log { filterx { x = enum(nosuch.thing); }; };",
    "log { filterx { x = \"${MSG\"; }; };",
    "log { filterx { x = nosuchfunction(1, 2); }; };",
    "log { filterx { x = len(a=1, 2); }; };",
  };

  for (gint i = 0; i < G_N_ELEMENTS(configs); i++)
    cr_assert(not(_parse(configs[i])), "%s", configs[i]);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(cfg_parse_errors, .init = setup, .fini = teardown);

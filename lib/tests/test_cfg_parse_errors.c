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
#include "libtest/config_parse_lib.h"

#include "cfg.h"
#include "cfg-grammar.h"
#include "apphook.h"

/* bison error recovery discards semantic values without freeing them, and
 * every input here is a syntax error */
const char *
__lsan_default_suppressions(void)
{
  return "leak:filterx_parse";
}

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
  cr_assert_not(_parse("log { filterx { x = ("));
}

Test(cfg_parse_errors, multi_line_error_location_past_the_line_end_is_reported)
{
  cr_assert_not(_parse("log { filterx "
                       "{ a =' d?\327\337? ;=\" \"\"bot0b\012; \012\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027\027 a =' d?\327\337? ;=\" \"; }"
                       " };"));
}

Test(cfg_parse_errors, invalid_regexp_in_a_match_sub_expression_is_a_parse_error)
{
  cr_assert_not(_parse("log { filterx { x = (\"a\" =~ \"(\"); }; };"));
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

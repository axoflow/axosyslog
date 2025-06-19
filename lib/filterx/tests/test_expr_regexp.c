/*
 * Copyright (c) 2024 Attila Szakacs
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
#include "libtest/filterx-lib.h"

#include "filterx/expr-regexp.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "compat/pcre.h"

static gboolean
_check_match(const gchar *lhs, const gchar *pattern)
{
  FilterXExpr *expr = filterx_expr_regexp_match_new(filterx_literal_new(filterx_string_new(lhs, -1)), pattern);

  FilterXObject *result_obj = filterx_expr_eval(expr);
  cr_assert(result_obj);
  gboolean result;
  cr_assert(filterx_boolean_unwrap(result_obj, &result));

  filterx_expr_unref(expr);
  filterx_object_unref(result_obj);

  return result;
}

static void
_assert_match(const gchar *lhs, const gchar *pattern)
{
  cr_assert(_check_match(lhs, pattern), "regexp should match. lhs: %s pattern: %s", lhs, pattern);
}

static void
_assert_not_match(const gchar *lhs, const gchar *pattern)
{
  cr_assert_not(_check_match(lhs, pattern), "regexp should not match. lhs: %s pattern: %s", lhs, pattern);
}

static void
_assert_match_init_error(const gchar *lhs, const gchar *pattern)
{
  cr_assert_not(filterx_expr_regexp_match_new(filterx_literal_new(filterx_string_new(lhs, -1)), pattern));
}

Test(filterx_expr_regexp, regexp_match)
{
  _assert_match("foo", "(?<key>foo)");
  _assert_match("foo", "(?<key>foo)|(?<key>bar)");
  _assert_not_match("abc", "Abc");
  _assert_not_match("abc", "(?<key>Abc)");
  _assert_match_init_error("abc", "(");
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

TestSuite(filterx_expr_regexp, .init = setup, .fini = teardown);

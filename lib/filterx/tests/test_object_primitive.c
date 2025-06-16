/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/filterx-object.h"
#include "filterx/object-primitive.h"
#include "filterx/object-message-value.h"
#include "apphook.h"

static void
_assert_repr_equals(FilterXObject *fobj, const gchar *expected)
{
  GString *repr = g_string_sized_new(0);
  filterx_object_repr(fobj, repr);
  cr_assert_str_eq(repr->str, expected);
  g_string_free(repr, TRUE);
}

Test(filterx_object, test_filterx_double_should_always_have_fraction_part)
{
  FilterXObject *fobj = filterx_double_new(4.0);
  _assert_repr_equals(fobj, "4.0");
  filterx_object_unref(fobj);
}

Test(filterx_object, test_filterx_double_trailing_zeroes_are_removed)
{
  /* we produce enough granularity when needed but do not produce trailing
   * zeroes */

  FilterXObject *fobj = filterx_double_new(4.1);
  _assert_repr_equals(fobj, "4.0999999999999996");
  filterx_object_unref(fobj);

  fobj = filterx_double_new(4.2);
  _assert_repr_equals(fobj, "4.2000000000000002");
  filterx_object_unref(fobj);

  fobj = filterx_double_new(4.5);
  _assert_repr_equals(fobj, "4.5");
  filterx_object_unref(fobj);
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

TestSuite(filterx_object, .init = setup, .fini = teardown);

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
#include "libtest/filterx-lib.h"

#include "filterx/json-repr.h"
#include "filterx/filterx-object.h"

#include "apphook.h"
#include "scratch-buffers.h"

#define NESTING_DEPTH 65000

Test(filterx_json_repr, deeply_nested_array_does_not_overflow_stack)
{
  GString *deep = g_string_sized_new(NESTING_DEPTH * 2 + 1);
  for (gint i = 0; i < NESTING_DEPTH; i++)
    g_string_append_c(deep, '[');
  for (gint i = 0; i < NESTING_DEPTH; i++)
    g_string_append_c(deep, ']');

  GError *error = NULL;
  FilterXObject *obj = filterx_object_from_json(deep->str, deep->len, &error);

  filterx_object_unref(obj);
  if (error)
    g_error_free(error);
  g_string_free(deep, TRUE);

  cr_assert(TRUE);
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

TestSuite(filterx_json_repr, .init = setup, .fini = teardown);

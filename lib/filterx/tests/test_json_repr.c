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

/* the legacy hardcoded jsmn token limit was 65536, use a token count comfortably above that */
#define LARGE_ARRAY_ELEMENT_COUNT 100000

Test(filterx_json_repr, array_exceeding_legacy_max_tokens_is_parsed_successfully)
{
  GString *large = g_string_sized_new(LARGE_ARRAY_ELEMENT_COUNT * 2 + 2);
  g_string_append_c(large, '[');
  for (gint i = 0; i < LARGE_ARRAY_ELEMENT_COUNT; i++)
    {
      if (i > 0)
        g_string_append_c(large, ',');
      g_string_append_c(large, '1');
    }
  g_string_append_c(large, ']');

  GError *error = NULL;
  FilterXObject *obj = filterx_object_from_json(large->str, large->len, &error);

  cr_assert_null(error, "%s", error ? error->message : "");
  cr_assert_not_null(obj);

  guint64 len = 0;
  cr_assert(filterx_object_len(obj, &len));
  cr_assert_eq(len, LARGE_ARRAY_ELEMENT_COUNT);

  filterx_object_unref(obj);
  g_string_free(large, TRUE);
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

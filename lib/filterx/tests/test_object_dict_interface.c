/*
 * Copyright (c) 2024 shifter <shifter@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "filterx/object-dict-interface.h"
#include "filterx/object-string.h"
#include "filterx/object-json.h"
#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_dict_interface, test_keys_empty)
{
  FilterXObject *json = filterx_json_new_from_repr("{}", -1);
  cr_assert_not_null(json);

  FilterXObject *keys = filterx_json_array_new_empty();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(json, &keys);
  cr_assert(ok);
  cr_assert_not_null(keys);

  GString *repr = scratch_buffers_alloc();
  cr_assert(filterx_object_repr(keys, repr));
  cr_assert_str_eq(repr->str, "[]");

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_single)
{
  FilterXObject *json = filterx_json_new_from_repr("{\"foo\":\"bar\"}", -1);
  cr_assert_not_null(json);

  FilterXObject *keys = filterx_json_array_new_empty();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(json, &keys);
  cr_assert(ok);
  cr_assert_not_null(keys);

  GString *repr = scratch_buffers_alloc();
  cr_assert(filterx_object_repr(keys, repr));
  cr_assert_str_eq("[\"foo\"]", repr->str);

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_multi)
{
  FilterXObject *json = filterx_json_new_from_repr("{\"foo\":\"bar\", \"bar\": \"baz\"}", -1);
  cr_assert_not_null(json);

  FilterXObject *keys = filterx_json_array_new_empty();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(json, &keys);
  cr_assert(ok);
  cr_assert_not_null(keys);

  GString *repr = scratch_buffers_alloc();
  cr_assert(filterx_object_repr(keys, repr));
  cr_assert_str_eq("[\"foo\",\"bar\"]", repr->str);

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_nested)
{
  FilterXObject *json = filterx_json_new_from_repr("{\"foo\":{\"bar\":\"baz\"}, \"bar\":{\"baz\": null}}", -1);
  cr_assert_not_null(json);

  FilterXObject *keys = filterx_json_array_new_empty();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(json, &keys);
  cr_assert(ok);
  cr_assert_not_null(keys);

  GString *repr = scratch_buffers_alloc();
  cr_assert(filterx_object_repr(keys, repr));
  cr_assert_str_eq("[\"foo\",\"bar\"]", repr->str);

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_null_dict, .signal=SIGABRT)
{
  FilterXObject *keys = filterx_json_array_new_empty();
  cr_assert_not_null(keys);

  filterx_dict_keys(NULL, &keys);

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_null_keys, .signal=SIGABRT)
{
  FilterXObject *json = filterx_json_new_from_repr("{\"foo\":{\"bar\":\"baz\"}, \"bar\":{\"baz\": null}}", -1);
  cr_assert_not_null(json);

  filterx_dict_keys(json, NULL);
}

Test(filterx_dict_interface, test_keys_key_type_is_not_list, .signal=SIGABRT)
{
  FilterXObject *json = filterx_json_new_from_repr("{\"foo\":{\"bar\":\"baz\"}, \"bar\":{\"baz\": null}}", -1);
  cr_assert_not_null(json);

  FilterXObject *keys = filterx_string_new("this is string", -1);
  cr_assert_not_null(keys);

  filterx_dict_keys(json, &keys);

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_object_is_not_dict)
{
  FilterXObject *json = filterx_string_new("{\"foo\":\"bar\", \"bar\": \"baz\"}", -1);
  cr_assert_not_null(json);

  FilterXObject *keys = filterx_json_array_new_empty();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(json, &keys);
  cr_assert(!ok);
  guint64 len = G_MAXUINT64;
  cr_assert(filterx_object_len(keys, &len));
  cr_assert_eq(0, len);
  filterx_object_unref(keys);
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

TestSuite(filterx_dict_interface, .init = setup, .fini = teardown);

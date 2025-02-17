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
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/json-repr.h"
#include "apphook.h"
#include "scratch-buffers.h"

static void
_set_key(FilterXObject *dict, const gchar *_key, const gchar *_value)
{
  FilterXObject *key = filterx_string_new(_key, -1);
  FilterXObject *value = filterx_string_new(_value, -1);
  filterx_object_set_subscript(dict, key, &value);

  filterx_object_unref(key);
  filterx_object_unref(value);
}

static FilterXObject *
_get_key(FilterXObject *dict, const gchar *_key)
{
  FilterXObject *key = filterx_string_new(_key, -1);
  FilterXObject *value = filterx_object_get_subscript(dict, key);
  filterx_object_unref(key);
  cr_assert(filterx_object_is_type(value, &FILTERX_TYPE_NAME(string)));
  return value;
}

static void
_assert_key_value(FilterXObject *dict, const gchar *key, const gchar *value)
{
  FilterXObject *v = _get_key(dict, key);
  cr_assert_str_eq(filterx_string_get_value_ref(v, NULL), value);
  filterx_object_unref(v);
}

Test(filterx_dict_interface, test_add_and_lookup_keys)
{
  FilterXObject *dict = filterx_object_from_json("{}", -1, NULL);
  cr_assert_not_null(dict);

  _set_key(dict, "foo", "bar");
  _set_key(dict, "foo2", "bar2");
  _assert_key_value(dict, "foo", "bar");
  _assert_key_value(dict, "foo2", "bar2");

  filterx_object_unref(dict);
}

Test(filterx_dict_interface, test_add_and_overwrite_keys)
{
  FilterXObject *dict = filterx_object_from_json("{}", -1, NULL);
  cr_assert_not_null(dict);

  _set_key(dict, "foo", "bar");
  _assert_key_value(dict, "foo", "bar");
  _set_key(dict, "foo", "bar2");
  _assert_key_value(dict, "foo", "bar2");

  filterx_object_unref(dict);
}

/* NOTE: to test hash table growth */
Test(filterx_dict_interface, test_add_and_lookup_lots_of_keys_to_test_hash_table_resize)
{
  FilterXObject *dict = filterx_object_from_json("{}", -1, NULL);
  cr_assert_not_null(dict);

  for (gint i = 0; i < 256; i++)
    {
      gchar key_buf[16], value_buf[16];
      g_snprintf(key_buf, sizeof(key_buf), "foo%03d", i);
      g_snprintf(value_buf, sizeof(value_buf), "bar%03d", i);
      _set_key(dict, key_buf, value_buf);
      _assert_key_value(dict, key_buf, value_buf);
    }

  filterx_object_unref(dict);
}

Test(filterx_dict_interface, test_keys_empty)
{
  FilterXObject *dict = filterx_object_from_json("{}", -1, NULL);
  cr_assert_not_null(dict);

  FilterXObject *keys = filterx_list_new();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(dict, &keys);
  cr_assert(ok);
  cr_assert_not_null(keys);

  assert_object_repr_equals(keys, "[]");

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_single)
{
  FilterXObject *dict = filterx_object_from_json("{\"foo\":\"bar\"}", -1, NULL);
  cr_assert_not_null(dict);

  FilterXObject *keys = filterx_list_new();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(dict, &keys);
  cr_assert(ok);
  cr_assert_not_null(keys);

  assert_object_repr_equals(keys, "[\"foo\"]");

  filterx_object_unref(keys);
}


Test(filterx_dict_interface, test_keys_multi)
{
  FilterXObject *dict = filterx_object_from_json("{\"foo\":\"bar\", \"bar\": \"baz\"}", -1, NULL);
  cr_assert_not_null(dict);

  FilterXObject *keys = filterx_list_new();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(dict, &keys);
  cr_assert(ok);
  cr_assert_not_null(keys);

  assert_object_repr_equals(keys, "[\"foo\",\"bar\"]");

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_nested)
{
  FilterXObject *dict = filterx_object_from_json("{\"foo\":{\"bar\":\"baz\"}, \"bar\":{\"baz\": null}}", -1, NULL);
  cr_assert_not_null(dict);

  FilterXObject *keys = filterx_list_new();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(dict, &keys);
  cr_assert(ok);
  cr_assert_not_null(keys);

  assert_object_repr_equals(keys, "[\"foo\",\"bar\"]");

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_null_dict, .signal=SIGABRT)
{
  FilterXObject *keys = filterx_list_new();
  cr_assert_not_null(keys);

  filterx_dict_keys(NULL, &keys);

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_null_keys, .signal=SIGABRT)
{
  FilterXObject *dict = filterx_object_from_json("{\"foo\":{\"bar\":\"baz\"}, \"bar\":{\"baz\": null}}", -1, NULL);
  cr_assert_not_null(dict);

  filterx_dict_keys(dict, NULL);
}

Test(filterx_dict_interface, test_keys_key_type_is_not_list, .signal=SIGABRT)
{
  FilterXObject *dict = filterx_object_from_json("{\"foo\":{\"bar\":\"baz\"}, \"bar\":{\"baz\": null}}", -1, NULL);
  cr_assert_not_null(dict);

  FilterXObject *keys = filterx_string_new("this is string", -1);
  cr_assert_not_null(keys);

  filterx_dict_keys(dict, &keys);

  filterx_object_unref(keys);
}

Test(filterx_dict_interface, test_keys_object_is_not_dict)
{
  FilterXObject *dict = filterx_string_new("{\"foo\":\"bar\", \"bar\": \"baz\"}", -1);
  cr_assert_not_null(dict);

  FilterXObject *keys = filterx_list_new();
  cr_assert_not_null(keys);

  gboolean ok = filterx_dict_keys(dict, &keys);
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

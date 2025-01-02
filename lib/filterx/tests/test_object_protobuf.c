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
#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/filterx-object-istype.h"
#include "filterx/expr-function.h"

#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_protobuf, test_filterx_protobuf_typecast_null_args)
{
  FilterXObject *obj = filterx_typecast_protobuf(NULL, NULL, 0);
  cr_assert_null(obj);
}

Test(filterx_protobuf, test_filterx_protobuf_typecast_empty_args)
{
  FilterXObject *args[] = { NULL };

  FilterXObject *obj = filterx_typecast_protobuf(NULL, args, 0);
  cr_assert_null(obj);
}

Test(filterx_protobuf, test_filterx_protobuf_typecast_null_arg)
{
  FilterXObject *args[] = { NULL };

  FilterXObject *obj = filterx_typecast_protobuf(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_protobuf, test_filterx_protobuf_typecast_null_object_arg)
{
  FilterXObject *args[] = { filterx_null_new() };

  FilterXObject *obj = filterx_typecast_protobuf(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_protobuf, test_filterx_protobuf_typecast_from_bytes)
{
  FilterXObject *args[] = { filterx_bytes_new("not valid \0protobuf!", 20) };

  FilterXObject *obj = filterx_typecast_protobuf(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(protobuf)));

  gsize size;
  const gchar *bytes = filterx_protobuf_get_value_ref(obj, &size);

  cr_assert(memcmp("not valid \0protobuf!", bytes, size) == 0);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_protobuf, test_filterx_protobuf_typecast_from_protobuf)
{
  FilterXObject *args[] = { filterx_protobuf_new("not valid \0protobuf!", 20) };

  FilterXObject *obj = filterx_typecast_protobuf(NULL, args, G_N_ELEMENTS(args));
  cr_assert_eq(args[0], obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(filterx_protobuf, .init = setup, .fini = teardown);

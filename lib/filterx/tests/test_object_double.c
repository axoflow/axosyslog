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

#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/object-primitive.h"
#include "filterx/object-datetime.h"
#include "filterx/filterx-object-istype.h"
#include "apphook.h"
#include "scratch-buffers.h"


Test(filterx_double, test_filterx_primitive_double_marshales_to_double_repr)
{
  FilterXObject *fobj = filterx_double_new(36.07);
  assert_marshaled_object(fobj, "36.07", LM_VT_DOUBLE);
  filterx_object_unref(fobj);
}

Test(filterx_double, test_filterx_primitive_double_is_mapped_to_a_json_float)
{
  FilterXObject *fobj = filterx_double_new(36.0);
  assert_object_json_equals(fobj, "36.0");
  filterx_object_unref(fobj);
}

Test(filterx_double, test_filterx_primitive_double_is_truthy_if_nonzero)
{
  FilterXObject *fobj = filterx_double_new(1);
  cr_assert(filterx_object_truthy(fobj));
  cr_assert_not(filterx_object_falsy(fobj));
  filterx_object_unref(fobj);

  fobj = filterx_double_new(0.0);
  cr_assert_not(filterx_object_truthy(fobj));
  cr_assert(filterx_object_falsy(fobj));
  filterx_object_unref(fobj);
}

Test(filterx_double, test_filterx_double_typecast_null_args)
{
  FilterXObject *obj = filterx_typecast_double(NULL, NULL, 0);
  cr_assert_null(obj);
}

Test(filterx_double, test_filterx_double_typecast_empty_args)
{
  FilterXObject *args[] = { NULL };
  FilterXObject *obj = filterx_typecast_double(NULL, args, 0);
  cr_assert_null(obj);
}

Test(filterx_double, test_filterx_double_typecast_null_arg)
{
  FilterXObject *args[] = { NULL };

  FilterXObject *obj = filterx_typecast_double(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);
}

Test(filterx_double, test_filterx_double_typecast_null_object_arg)
{
  FilterXObject *args[] = { filterx_null_new() };

  FilterXObject *obj = filterx_typecast_double(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_double, test_filterx_double_typecast_from_double)
{
  FilterXObject *args[] = { filterx_double_new(3.14) };

  FilterXObject *obj = filterx_typecast_double(NULL, args, G_N_ELEMENTS(args));
  cr_assert_eq(args[0], obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_double, test_filterx_double_typecast_from_integer)
{
  FilterXObject *args[] = { filterx_integer_new(443) };

  FilterXObject *obj = filterx_typecast_double(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(double)));

  GenericNumber gn = filterx_primitive_get_value(obj);
  cr_assert_float_eq(443.0, gn_as_double(&gn), 0.00001);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}


Test(filterx_double, test_filterx_double_typecast_from_string)
{
  FilterXObject *args[] = { filterx_string_new("443.117", -1) };

  FilterXObject *obj = filterx_typecast_double(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(double)));

  GenericNumber gn = filterx_primitive_get_value(obj);
  cr_assert_float_eq(443.117, gn_as_double(&gn), 0.00001);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_double, test_filterx_double_typecast_from_datetime)
{
  UnixTime ut = { .ut_sec = 171, .ut_usec = 443221 };
  FilterXObject *args[] = { filterx_datetime_new(&ut) };

  FilterXObject *obj = filterx_typecast_double(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(double)));

  GenericNumber gn = filterx_primitive_get_value(obj);
  cr_assert_float_eq(171.443221, gn_as_double(&gn), 0.00001);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_double, test_filterx_double_repr)
{
  FilterXObject *obj = filterx_double_new(123.456);
  GString *repr = scratch_buffers_alloc();
  g_string_assign(repr, "foo");
  cr_assert(filterx_object_repr(obj, repr));
  cr_assert_str_eq("123.456", repr->str);
  cr_assert(filterx_object_repr_append(obj, repr));
  cr_assert_str_eq("123.456123.456", repr->str);
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

TestSuite(filterx_double, .init = setup, .fini = teardown);

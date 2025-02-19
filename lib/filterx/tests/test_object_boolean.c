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
#include "filterx/expr-function.h"

#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_boolean, test_filterx_primitive_bool_marshales_to_bool_repr)
{
  FilterXObject *fobj = filterx_boolean_new(TRUE);
  assert_marshaled_object(fobj, "true", LM_VT_BOOLEAN);
  filterx_object_unref(fobj);
}

Test(filterx_boolean, test_filterx_primitive_bool_is_mapped_to_a_json_bool)
{
  FilterXObject *fobj = filterx_boolean_new(TRUE);
  assert_object_json_equals(fobj, "true");
  filterx_object_unref(fobj);

  fobj = filterx_boolean_new(FALSE);
  assert_object_json_equals(fobj, "false");
  filterx_object_unref(fobj);
}

Test(filterx_boolean, test_filterx_primitive_bool_is_truthy_if_true)
{
  FilterXObject *fobj = filterx_boolean_new(TRUE);
  cr_assert(filterx_object_truthy(fobj));
  cr_assert_not(filterx_object_falsy(fobj));
  filterx_object_unref(fobj);

  fobj = filterx_boolean_new(0.0);
  cr_assert_not(filterx_object_truthy(fobj));
  cr_assert(filterx_object_falsy(fobj));
  filterx_object_unref(fobj);
}

Test(filterx_boolean, test_filterx_boolean_typecast_null_args)
{
  FilterXObject *obj = filterx_typecast_boolean(NULL, NULL, 0);
  cr_assert_null(obj);
}

Test(filterx_boolean, test_filterx_boolean_typecast_empty_args)
{
  FilterXObject *args[] = { NULL };

  FilterXObject *obj = filterx_typecast_boolean(NULL, args, 0);
  cr_assert_null(obj);
}

Test(filterx_boolean, test_filterx_boolean_typecast_null_arg)
{
  FilterXObject *args[] = { NULL };

  FilterXObject *obj = filterx_typecast_boolean(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_boolean, test_filterx_boolean_typecast_null_object_arg)
{
  FilterXObject *args[] = { filterx_null_new() };

  FilterXObject *obj = filterx_typecast_boolean(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(boolean)));

  cr_assert(!filterx_object_truthy(obj));

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_boolean, test_filterx_boolean_typecast_from_boolean)
{
  FilterXObject *args[] = { filterx_boolean_new(TRUE) };

  FilterXObject *obj = filterx_typecast_boolean(NULL, args, G_N_ELEMENTS(args));
  cr_assert_eq(args[0], obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
  filterx_object_unref(obj);
}

Test(filterx_boolean, test_filterx_boolean_repr)
{
  FilterXObject *obj = filterx_boolean_new(TRUE);
  assert_object_repr_equals(obj, "true");
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

TestSuite(filterx_boolean, .init = setup, .fini = teardown);

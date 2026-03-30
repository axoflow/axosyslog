/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/object-subnet.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-object.h"
#include "filterx/expr-function.h"

#include "apphook.h"
#include "scratch-buffers.h"

/* Construction */

Test(filterx_object_subnet, ipv4_prefix_notation)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("192.168.0.0/24");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(subnet)));
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, ipv4_netmask_notation)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("192.168.0.0/255.255.255.0");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(subnet)));
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, ipv4_host_without_prefix)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("192.168.1.1");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(subnet)));
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, ipv6_prefix_notation)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("2001:db8::/32");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(subnet)));
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, ipv6_host_without_prefix)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("::1");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(subnet)));
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, rejects_invalid_cidr)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("not-a-subnet");
  cr_assert_null(obj);
}

Test(filterx_object_subnet, rejects_ipv4_prefix_too_large)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("192.168.0.0/33");
  cr_assert_null(obj);
}

Test(filterx_object_subnet, rejects_ipv6_prefix_too_large)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("2001:db8::/129");
  cr_assert_null(obj);
}

Test(filterx_object_subnet, ipv4_host_bits_are_masked)
{
  /* 192.168.0.5/24 should store 192.168.0.0/255.255.255.0 */
  FilterXObject *obj = filterx_subnet_new_from_cidr("192.168.0.5/24");
  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "192.168.0.0/255.255.255.0", LM_VT_STRING);
  filterx_object_unref(obj);
}

/* Marshal / repr */

Test(filterx_object_subnet, ipv4_marshal)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("192.168.0.0/24");
  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "192.168.0.0/255.255.255.0", LM_VT_STRING);
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, ipv4_host_marshal)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("10.0.0.1");
  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "10.0.0.1/255.255.255.255", LM_VT_STRING);
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, ipv4_repr)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("10.0.0.0/8");
  cr_assert_not_null(obj);
  assert_object_repr_equals(obj, "subnet('10.0.0.0/255.0.0.0')");
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, ipv6_marshal)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("2001:db8::/32");
  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "2001:db8::/ffff:ffff::", LM_VT_STRING);
  filterx_object_unref(obj);
}

Test(filterx_object_subnet, ipv4_format_json)
{
  FilterXObject *obj = filterx_subnet_new_from_cidr("172.16.0.0/12");
  cr_assert_not_null(obj);
  assert_object_json_equals(obj, "\"172.16.0.0/255.240.0.0\"");
  filterx_object_unref(obj);
}

/* Membership */

Test(filterx_object_subnet, ipv4_member_returns_true)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *ip = filterx_string_new("192.168.0.100", -1);

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(result, &b));
  cr_assert(b);

  filterx_object_unref(result);
  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_subnet, ipv4_non_member_returns_false)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *ip = filterx_string_new("192.168.1.1", -1);

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(result, &b));
  cr_assert_not(b);

  filterx_object_unref(result);
  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_subnet, ipv4_boundary_addresses)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("10.0.0.0/8");
  FilterXObject *first = filterx_string_new("10.0.0.0", -1);
  FilterXObject *last  = filterx_string_new("10.255.255.255", -1);
  FilterXObject *beyond = filterx_string_new("11.0.0.0", -1);

  FilterXObject *r1 = filterx_object_is_member_of(subnet, first);
  FilterXObject *r2 = filterx_object_is_member_of(subnet, last);
  FilterXObject *r3 = filterx_object_is_member_of(subnet, beyond);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(r1, &b) && b);
  cr_assert(filterx_boolean_unwrap(r2, &b) && b);
  cr_assert(filterx_boolean_unwrap(r3, &b) && !b);

  filterx_object_unref(r1);
  filterx_object_unref(r2);
  filterx_object_unref(r3);
  filterx_object_unref(first);
  filterx_object_unref(last);
  filterx_object_unref(beyond);
  filterx_object_unref(subnet);
}

Test(filterx_object_subnet, ipv6_member_returns_true)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("2001:db8::/32");
  FilterXObject *ip = filterx_string_new("2001:db8::1", -1);

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(result, &b));
  cr_assert(b);

  filterx_object_unref(result);
  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_subnet, ipv6_non_member_returns_false)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("2001:db8::/32");
  FilterXObject *ip = filterx_string_new("2001:db9::1", -1);

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(result, &b));
  cr_assert_not(b);

  filterx_object_unref(result);
  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_subnet, member_of_rejects_non_string)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *not_a_string = filterx_integer_new(42);

  FilterXObject *result = filterx_object_is_member_of(subnet, not_a_string);
  cr_assert_null(result);

  filterx_object_unref(not_a_string);
  filterx_object_unref(subnet);
}

Test(filterx_object_subnet, member_of_rejects_invalid_ip)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *bad_ip = filterx_string_new("not-an-ip", -1);

  FilterXObject *result = filterx_object_is_member_of(subnet, bad_ip);
  cr_assert_null(result);

  filterx_object_unref(bad_ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_subnet, ipv4_subnet_rejects_ipv6_address)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *ipv6 = filterx_string_new("2001:db8::1", -1);

  FilterXObject *result = filterx_object_is_member_of(subnet, ipv6);
  cr_assert_null(result);

  filterx_object_unref(ipv6);
  filterx_object_unref(subnet);
}

Test(filterx_object_subnet, ipv6_subnet_rejects_ipv4_address)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("2001:db8::/32");
  FilterXObject *ipv4 = filterx_string_new("192.168.0.1", -1);

  FilterXObject *result = filterx_object_is_member_of(subnet, ipv4);
  cr_assert_null(result);

  filterx_object_unref(ipv4);
  filterx_object_unref(subnet);
}

/* Typecast */

Test(filterx_object_subnet, typecast_from_string)
{
  FilterXObject *args[] = { filterx_string_new("192.168.0.0/24", -1) };

  FilterXObject *obj = filterx_typecast_subnet(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(subnet)));
  assert_marshaled_object(obj, "192.168.0.0/255.255.255.0", LM_VT_STRING);

  filterx_object_unref(obj);
  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_subnet, typecast_identity)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *args[] = { filterx_object_ref(subnet) };

  FilterXObject *obj = filterx_typecast_subnet(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(obj == subnet);

  filterx_object_unref(obj);
  filterx_object_unref(subnet);
  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_subnet, typecast_rejects_integer)
{
  FilterXObject *args[] = { filterx_integer_new(42) };

  FilterXObject *obj = filterx_typecast_subnet(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_subnet, typecast_rejects_invalid_cidr_string)
{
  FilterXObject *args[] = { filterx_string_new("not-a-cidr", -1) };

  FilterXObject *obj = filterx_typecast_subnet(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_subnet, typecast_rejects_empty_args)
{
  FilterXObject *obj = filterx_typecast_subnet(NULL, NULL, 0);
  cr_assert_null(obj);
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

TestSuite(filterx_object_subnet, .init = setup, .fini = teardown);

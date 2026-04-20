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

#include "filterx/object-ip.h"
#include "filterx/object-subnet.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-object.h"
#include "filterx/expr-function.h"

#include "apphook.h"
#include "scratch-buffers.h"

/* Construction */

Test(filterx_object_ip, ipv4_valid)
{
  FilterXObject *obj = filterx_ip_new_from_string("192.168.1.1");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(ip)));
  filterx_object_unref(obj);
}

Test(filterx_object_ip, ipv6_valid)
{
  FilterXObject *obj = filterx_ip_new_from_string("2001:db8::1");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(ip)));
  filterx_object_unref(obj);
}

Test(filterx_object_ip, ipv4_loopback)
{
  FilterXObject *obj = filterx_ip_new_from_string("127.0.0.1");
  cr_assert_not_null(obj);
  filterx_object_unref(obj);
}

Test(filterx_object_ip, ipv6_loopback)
{
  FilterXObject *obj = filterx_ip_new_from_string("::1");
  cr_assert_not_null(obj);
  filterx_object_unref(obj);
}

Test(filterx_object_ip, rejects_invalid_string)
{
  FilterXObject *obj = filterx_ip_new_from_string("not-an-ip");
  cr_assert_null(obj);
}

Test(filterx_object_ip, rejects_cidr_notation)
{
  FilterXObject *obj = filterx_ip_new_from_string("192.168.0.0/24");
  cr_assert_null(obj);
}

Test(filterx_object_ip, rejects_ipv6_cidr_notation)
{
  FilterXObject *obj = filterx_ip_new_from_string("2001:db8::/32");
  cr_assert_null(obj);
}

/* Truthy */

Test(filterx_object_ip, ip_is_always_truthy)
{
  FilterXObject *obj = filterx_ip_new_from_string("192.168.1.1");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_truthy(obj));
  filterx_object_unref(obj);

  obj = filterx_ip_new_from_string("0.0.0.0");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_truthy(obj));
  filterx_object_unref(obj);

  obj = filterx_ip_new_from_string("::1");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_truthy(obj));
  filterx_object_unref(obj);

  obj = filterx_ip_new_from_string("::");
  cr_assert_not_null(obj);
  cr_assert(filterx_object_truthy(obj));
  filterx_object_unref(obj);
}

/* Marshal / repr */

Test(filterx_object_ip, ipv4_marshal)
{
  FilterXObject *obj = filterx_ip_new_from_string("10.0.0.1");
  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "10.0.0.1", LM_VT_STRING);
  filterx_object_unref(obj);
}

Test(filterx_object_ip, ipv6_marshal)
{
  FilterXObject *obj = filterx_ip_new_from_string("2001:db8::1");
  cr_assert_not_null(obj);
  assert_marshaled_object(obj, "2001:db8::1", LM_VT_STRING);
  filterx_object_unref(obj);
}

Test(filterx_object_ip, ipv4_repr)
{
  FilterXObject *obj = filterx_ip_new_from_string("192.168.0.1");
  cr_assert_not_null(obj);
  assert_object_repr_equals(obj, "ip('192.168.0.1')");
  filterx_object_unref(obj);
}

Test(filterx_object_ip, ipv4_format_json)
{
  FilterXObject *obj = filterx_ip_new_from_string("172.16.0.1");
  cr_assert_not_null(obj);
  assert_object_json_equals(obj, "\"172.16.0.1\"");
  filterx_object_unref(obj);
}

/* Membership via subnet */

Test(filterx_object_ip, ipv4_ip_member_of_subnet)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *ip = filterx_ip_new_from_string("192.168.0.100");

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(result, &b));
  cr_assert(b);

  filterx_object_unref(result);
  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_ip, ipv4_ip_not_member_of_subnet)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *ip = filterx_ip_new_from_string("192.168.1.1");

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(result, &b));
  cr_assert_not(b);

  filterx_object_unref(result);
  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_ip, ipv6_ip_member_of_subnet)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("2001:db8::/32");
  FilterXObject *ip = filterx_ip_new_from_string("2001:db8::1");

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(result, &b));
  cr_assert(b);

  filterx_object_unref(result);
  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_ip, ipv6_ip_not_member_of_subnet)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("2001:db8::/32");
  FilterXObject *ip = filterx_ip_new_from_string("2001:db9::1");

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);

  gboolean b;
  cr_assert(filterx_boolean_unwrap(result, &b));
  cr_assert_not(b);

  filterx_object_unref(result);
  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_ip, ipv4_subnet_rejects_ipv6_ip)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("192.168.0.0/24");
  FilterXObject *ip = filterx_ip_new_from_string("2001:db8::1");

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);
  cr_assert(filterx_object_falsy(result));

  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

Test(filterx_object_ip, ipv6_subnet_rejects_ipv4_ip)
{
  FilterXObject *subnet = filterx_subnet_new_from_cidr("2001:db8::/32");
  FilterXObject *ip = filterx_ip_new_from_string("192.168.0.1");

  FilterXObject *result = filterx_object_is_member_of(subnet, ip);
  cr_assert_not_null(result);
  cr_assert(filterx_object_falsy(result));

  filterx_object_unref(ip);
  filterx_object_unref(subnet);
}

/* Typecast */

Test(filterx_object_ip, typecast_from_string)
{
  FilterXObject *args[] = { filterx_string_new("192.168.0.1", -1) };

  FilterXObject *obj = filterx_typecast_ip(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(ip)));
  assert_marshaled_object(obj, "192.168.0.1", LM_VT_STRING);

  filterx_object_unref(obj);
  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_ip, typecast_ipv6_from_string)
{
  FilterXObject *args[] = { filterx_string_new("::1", -1) };

  FilterXObject *obj = filterx_typecast_ip(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(ip)));
  assert_marshaled_object(obj, "::1", LM_VT_STRING);

  filterx_object_unref(obj);
  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_ip, typecast_identity)
{
  FilterXObject *ip = filterx_ip_new_from_string("10.0.0.1");
  FilterXObject *args[] = { filterx_object_ref(ip) };

  FilterXObject *obj = filterx_typecast_ip(NULL, args, G_N_ELEMENTS(args));
  cr_assert_not_null(obj);
  cr_assert(obj == ip);

  filterx_object_unref(obj);
  filterx_object_unref(ip);
  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_ip, typecast_rejects_integer)
{
  FilterXObject *args[] = { filterx_integer_new(42) };

  FilterXObject *obj = filterx_typecast_ip(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_ip, typecast_rejects_cidr_string)
{
  FilterXObject *args[] = { filterx_string_new("192.168.0.0/24", -1) };

  FilterXObject *obj = filterx_typecast_ip(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_ip, typecast_rejects_invalid_string)
{
  FilterXObject *args[] = { filterx_string_new("not-an-ip", -1) };

  FilterXObject *obj = filterx_typecast_ip(NULL, args, G_N_ELEMENTS(args));
  cr_assert_null(obj);

  filterx_simple_function_free_args(args, G_N_ELEMENTS(args));
}

Test(filterx_object_ip, typecast_rejects_empty_args)
{
  FilterXObject *obj = filterx_typecast_ip(NULL, NULL, 0);
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

TestSuite(filterx_object_ip, .init = setup, .fini = teardown);

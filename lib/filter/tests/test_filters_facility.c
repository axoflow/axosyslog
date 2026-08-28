/*
 * Copyright (c) 2018 Balabit
 * Copyright (c) 2013 Gergely Nagy <algernon@balabit.hu>
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
#include "libtest/parameterized.h"
#include "test_filters_common.h"

#include "filter/filter-expr.h"
#include "filter/filter-re.h"
#include "filter/filter-pri.h"
#include "cfg.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

TestSuite(filter, .init = setup, .fini = teardown);

typedef struct _FilterParamBits
{
  const gchar *msg;
  const gchar *fac_str;
  gboolean    expected_result;
} FilterParamBits;

static FilterParamBits test_filter_facility_str_params[] =
{
  {.msg = "<15> openvpn[2499]: PTHREAD support initialized", .fac_str = "user", .expected_result = TRUE},
  {.msg = "<15> openvpn[2499]: PTHREAD support initialized", .fac_str = "daemon", .expected_result = FALSE},
  {.msg = "<2> openvpn[2499]: PTHREAD support initialized",  .fac_str = "kern", .expected_result = TRUE},
  {.msg = "<128> openvpn[2499]: PTHREAD support initialized", .fac_str = "local0", .expected_result = TRUE},
  {.msg = "<32> openvpn[2499]: PTHREAD support initialized", .fac_str = "local1", .expected_result = FALSE},
  {.msg = "<32> openvpn[2499]: PTHREAD support initialized", .fac_str = "auth", .expected_result = TRUE},
#ifdef LOG_AUTHPRIV
  {.msg = "<80> openvpn[2499]: PTHREAD support initialized", .fac_str = "authpriv", .expected_result = TRUE},
#endif
};

StaticParameterizedTest(FilterParamBits *param, test_filter_facility_str_params, filter, test_filter_facility_str)
{
  FilterExprNode *filter = filter_facility_new(facility_bits(param->fac_str));
  testcase(param->msg, filter, param->expected_result);
}

typedef struct _FilterParamFacilities
{
  const gchar *msg;
  guint32     facilities;
  gboolean    expected_result;
} FilterParamFacilities;

static FilterParamFacilities test_filter_facility_params[] =
{
  {.msg = "<15> openvpn[2499]: PTHREAD support initialized", .facilities = 0x80000000 | (LOG_USER >> 3), .expected_result = TRUE},
  {.msg = "<15> openvpn[2499]: PTHREAD support initialized", .facilities = 0x80000000 | (LOG_DAEMON >> 3), .expected_result = FALSE},
  {.msg = "<2> openvpn[2499]: PTHREAD support initialized",  .facilities = 0x80000000 | (LOG_KERN >> 3), .expected_result = TRUE},
  {.msg = "<128> openvpn[2499]: PTHREAD support initialized", .facilities = 0x80000000 | (LOG_LOCAL0 >> 3), .expected_result = TRUE},
  {.msg = "<32> openvpn[2499]: PTHREAD support initialized", .facilities = 0x80000000 | (LOG_AUTH >> 3), .expected_result = TRUE},
#ifdef LOG_AUTHPRIV
  {.msg = "<80> openvpn[2499]: PTHREAD support initialized", .facilities = 0x80000000 | (LOG_AUTHPRIV >> 3), .expected_result = TRUE},
#endif
};

StaticParameterizedTest(FilterParamFacilities *param, test_filter_facility_params, filter, test_filter_facility)
{
  FilterExprNode *filter = filter_facility_new(param->facilities);
  testcase(param->msg, filter, param->expected_result);
}

Test(filter, test_filter_facility_bits)
{
  testcase("<15> openvpn[2499]: PTHREAD support initialized",
           filter_facility_new(facility_bits("daemon") | facility_bits("user")), TRUE);
  testcase("<15> openvpn[2499]: PTHREAD support initialized",
           filter_facility_new(facility_bits("uucp") | facility_bits("local4")), FALSE);
}

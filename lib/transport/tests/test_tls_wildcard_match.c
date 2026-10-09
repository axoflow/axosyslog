/*
 * Copyright (c) 2024 One Identity LLC.
 * Copyright (c) 2024 Franco Fichtner
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
#include <criterion/new/assert.h>

#include "transport/tls-verifier.h"

TestSuite(tls_wildcard, .init = NULL, .fini = NULL);

Test(tls_wildcard, test_wildcard_match_pattern_acceptance)
{
  cr_assert(tls_wildcard_match("test", "test"));
  cr_assert(tls_wildcard_match("test", "*"));
  cr_assert(tls_wildcard_match("test", "t*t"));
  cr_assert(tls_wildcard_match("test", "t*"));
  cr_assert(tls_wildcard_match("", ""));
  cr_assert(tls_wildcard_match("test.one", "test.one"));
  cr_assert(tls_wildcard_match("test.one.two", "test.one.two"));
  cr_assert(tls_wildcard_match("192.0.2.0", "192.0.2.0"));
  cr_assert(tls_wildcard_match("2001:0000:130F:0000:0000:09C0:876A:130B", "2001:0000:130F:0000:0000:09C0:876A:130B"));
  cr_assert(tls_wildcard_match("2001:0000:130F:0000:0000:09C0:876A:130B", "2001:0:130F:0:0:9C0:876A:130B"));
  cr_assert(tls_wildcard_match("2001:0:130F:0:0:9C0:876A:130B", "2001:0000:130F:0000:0000:09C0:876A:130B"));
  cr_assert(tls_wildcard_match("2001:0000:130F::09C0:876A:130B", "2001:0000:130F:0000:0000:09C0:876A:130B"));
  cr_assert(tls_wildcard_match("2001:0000:130F:0000:0000:09C0:876A:130B", "2001:0000:130F::09C0:876A:130B"));
  cr_assert(tls_wildcard_match("2001:0000:130F:0000:0000:09C0:876A:130B", "2001:0:130F::9C0:876A:130B"));
  cr_assert(tls_wildcard_match("2001:0:130F::9C0:876A:130B", "2001:0000:130F:0000:0000:09C0:876A:130B"));
}

Test(tls_wildcard, test_wildcard_match_wildcard_rejection)
{
  cr_assert(not(tls_wildcard_match("test", "**")));
  cr_assert(not(tls_wildcard_match("test", "*es*")));
  cr_assert(not(tls_wildcard_match("test", "t*?")));
}

Test(tls_wildcard, test_wildcard_match_pattern_rejection)
{
  cr_assert(not(tls_wildcard_match("test", "tset")));
  cr_assert(not(tls_wildcard_match("test", "set")));
  cr_assert(not(tls_wildcard_match("", "*")));
  cr_assert(not(tls_wildcard_match("test", "")));
  cr_assert(not(tls_wildcard_match("test.two", "test.one")));
}

Test(tls_wildcard, test_wildcard_match_format_rejection)
{
  cr_assert(not(tls_wildcard_match("test.two", "test.*")));
  cr_assert(not(tls_wildcard_match("test.two", "test.t*o")));
  cr_assert(not(tls_wildcard_match("test", "test.two")));
  cr_assert(not(tls_wildcard_match("test.two", "test")));
  cr_assert(not(tls_wildcard_match("test.one.two", "test.one")));
  cr_assert(not(tls_wildcard_match("test.one", "test.one.two")));
  cr_assert(not(tls_wildcard_match("test.three", "three.test")));
  cr_assert(not(tls_wildcard_match("test.one.two", "test.one.*")));
}

Test(tls_wildcard, test_wildcard_match_complex_rejection)
{
  cr_assert(not(tls_wildcard_match("test.two", "test.???")));
  cr_assert(not(tls_wildcard_match("test.one.two", "test.one.?wo")));
}

Test(tls_wildcard, test_ip_wildcard_rejection)
{
  cr_assert(not(tls_wildcard_match("192.0.2.0", "*.0.2.0")));
  cr_assert(not(tls_wildcard_match("2001:0000:130F:0000:0000:09C0:876A:130B", "*:0000:130F:0000:0000:09C0:876A:130B")));
  cr_assert(not(tls_wildcard_match("2001:0:130F::9C0:876A:130B", "*:0000:130F:0000:0000:09C0:876A:130B")));
}

Test(tls_wildcard, test_case_insensivity)
{
  cr_assert(tls_wildcard_match("test", "TEST"));
  cr_assert(tls_wildcard_match("TEST", "test"));
  cr_assert(tls_wildcard_match("TeST", "TEst"));
  cr_assert(tls_wildcard_match("test.one", "test.ONE"));
  cr_assert(tls_wildcard_match("test.TWO", "test.two"));
  cr_assert(tls_wildcard_match("test.three", "*T.three"));
  cr_assert(tls_wildcard_match("2001:0000:130F:0000:0000:09C0:876A:130B", "2001:0000:130f:0000:0000:09c0:876a:130b"));
}

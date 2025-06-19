/*
 * Copyright (c) 2019 Balabit
 * Copyright (c) 2019 Kokan <kokaipeter@gmail.com>
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

#include "pragma-parser.h"

Test(pragma_parser, process_valid_major_minor)
{
  const guint version = process_version_string("3.27");

  cr_assert_eq(0x031b, version);
}

Test(pragma_parser, process_version_large_minor)
{
  guint version = process_version_string("42.4294957319");

  cr_assert_eq(version, 0);
}

Test(pragma_parser, process_version_large_major)
{
  guint version = process_version_string("4294967299.7");

  cr_assert_eq(version, 0);
}

Test(pragma_parser, process_version_overflow_major)
{
  guint version = process_version_string("72057594037927939.7");

  cr_assert_eq(version, 0);
}

Test(pragma_parser, process_version_invalid_minor)
{
  guint version = process_version_string("4.x");

  cr_assert_eq(version, 0);
}

Test(pragma_parser, process_version_random_suffix)
{
  guint version = process_version_string("3.7.6.5.4.3.2.1.ignition.orbital.launch-successful!");

  cr_assert_eq(version, 0);
}

Test(pragma_parser, process_version_random_prefix)
{
  guint version = process_version_string(".+3.7");

  cr_assert_eq(version, 0);
}

Test(pragma_parser, process_version_negative_major)
{
  guint version = process_version_string("-1.1031");

  cr_assert_eq(version, 0);
}

Test(pragma_parser, process_version_negative_minor)
{
  guint version = process_version_string("42.-9977");

  cr_assert_eq(version, 0);
}

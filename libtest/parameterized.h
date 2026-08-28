/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 László Várady
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

#ifndef LIBTEST_PARAMETERIZED_H_INCLUDED
#define LIBTEST_PARAMETERIZED_H_INCLUDED 1

#include "syslog-ng.h"

#include <criterion/parameterized.h>

struct criterion_test_params static_parameterized_test_params(gsize n_params);

#define StaticParameterizedTest(Param, Array, Suite, Name, ...) \
  ParameterizedTestParameters(Suite, Name) \
  { \
    return static_parameterized_test_params(G_N_ELEMENTS(Array)); \
  } \
  static void Suite##_##Name##_body(Param); \
  ParameterizedTest(gsize *param_index__, Suite, Name, ##__VA_ARGS__) \
  { \
    Suite##_##Name##_body(&(Array)[*param_index__]); \
  } \
  static void Suite##_##Name##_body(Param)

#endif

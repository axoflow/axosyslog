/*
 * Copyright (c) 2024 Attila Szakacs
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

#ifndef LIBTEST_FILTERX_LIB_H_INCLUDED
#define LIBTEST_FILTERX_LIB_H_INCLUDED

#include "filterx/filterx-object.h"
#include "filterx/filterx-expr.h"

void assert_marshaled_object(FilterXObject *obj, const gchar *repr, LogMessageValueType type);
void assert_object_json_equals(FilterXObject *obj, const gchar *expected_json_repr);
void assert_object_repr_equals(FilterXObject *obj, const gchar *expected_repr);
void assert_object_str_equals(FilterXObject *obj, const gchar *expected_repr);

FILTERX_DECLARE_TYPE(test_dict);
FILTERX_DECLARE_TYPE(test_list);
FILTERX_DECLARE_TYPE(test_unknown_object);

FilterXObject *filterx_test_dict_new(void);
FilterXObject *filterx_test_list_new(void);
FilterXObject *filterx_test_unknown_object_new(void);

const gchar *filterx_test_unknown_object_marshaled_repr(gssize *len);
const gchar *filterx_test_unknown_object_repr(gssize *len);

FilterXExpr *filterx_non_literal_new(FilterXObject *object);
FilterXExpr *filterx_dummy_error_new(const gchar *msg);

void init_libtest_filterx(void);
void deinit_libtest_filterx(void);

#endif

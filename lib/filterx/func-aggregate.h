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

#ifndef FILTERX_FUNC_AGGREGATE_H_INCLUDED
#define FILTERX_FUNC_AGGREGATE_H_INCLUDED

#include "filterx/expr-function.h"

FilterXExpr *filterx_function_aggregate_new(FilterXFunctionArgs *args, GError **error);

/* for tests only: synchronously runs the timeout/replay logic for @key, as
 * if its timer had just fired. Returns FALSE if there's no pending entry
 * for @key. */
gboolean filterx_function_aggregate_test_expire(FilterXExpr *s, FilterXObject *key);

#endif

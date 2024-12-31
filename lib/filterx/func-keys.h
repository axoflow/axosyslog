/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter
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

#ifndef FILTERX_FUNC_KEYS_H_INCLUDED
#define FILTERX_FUNC_KEYS_H_INCLUDED

#include "filterx/expr-function.h"

#define FILTERX_FUNC_KEYS_USAGE "Usage: keys(dict)"
#define FILTERX_FUNC_KEYS_ERR_NONDICT "Argument must be a dict. "
#define FILTERX_FUNC_KEYS_ERR_NUM_ARGS "Invalid number of arguments. "

FilterXExpr *filterx_function_keys_new(FilterXFunctionArgs *args, GError **error);

#endif

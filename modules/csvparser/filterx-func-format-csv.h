/*
 * Copyright (c) 2024 shifter
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

#ifndef FILTERX_FUNC_FORMAT_CSV_H_INCLUDED
#define FILTERX_FUNC_FORMAT_CSV_H_INCLUDED


#include "plugin.h"
#include "filterx/expr-function.h"

#define FILTERX_FUNC_FORMAT_CSV_ARG_NAME_COLUMNS "columns"
#define FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DELIMITER "delimiter"
#define FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DEFAULT_VALUE "default_value"

FILTERX_FUNCTION_DECLARE(format_csv);

FilterXExpr *filterx_function_format_csv_new(FilterXFunctionArgs *args, GError **error);

#endif

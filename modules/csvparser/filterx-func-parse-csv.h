/*
 * Copyright (c) 2024 shifter
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef FILTERX_FUNC_PARSE_CSV_H_INCLUDED
#define FILTERX_FUNC_PARSE_CSV_H_INCLUDED

#include "plugin.h"
#include "filterx/expr-function.h"

#define FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS "columns"
#define FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER "delimiter"
#define FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS "string_delimiters"
#define FILTERX_FUNC_PARSE_CSV_ARG_NAME_DIALECT "dialect"
#define FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRIP_WHITESPACE "strip_whitespace"
#define FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRIP_WHITESPACES "strip_whitespaces"
#define FILTERX_FUNC_PARSE_CSV_ARG_NAME_GREEDY "greedy"
#define FILTERX_FUNC_PARSE_CSV_USAGE "Usage: parse_csv(msg_str [" \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS"=json_array, " \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER"=string, " \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS"=json_array, " \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_DIALECT"=string, " \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRIP_WHITESPACE"=boolean, " \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_GREEDY"=boolean])"
#define FILTERX_FUNC_PARSE_ERR_EMPTY_DELIMITER "Either '" \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER"' or '" \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS"' must be set, and '" \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER"' cannot be empty if '" \
    FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS"' is unset"

FILTERX_FUNCTION_DECLARE(parse_csv);

FilterXExpr *filterx_function_parse_csv_new(const gchar *function_name, FilterXFunctionArgs *args, GError **error);

#endif

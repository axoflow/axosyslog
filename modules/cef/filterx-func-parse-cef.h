/*
 * Copyright (c) 2023 Axoflow
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

#ifndef FILTERX_FUNC_PARSE_CEF_H_INCLUDED
#define FILTERX_FUNC_PARSE_CEF_H_INCLUDED

#include "plugin.h"
#include "filterx/expr-function.h"

#define FILTERX_FUNC_PARSE_CEF_USAGE "Usage: parse_cef(str " \
        EVENT_FORMAT_PARSER_ARG_NAME_PAIR_SEPARATOR"=boolean, " \
        EVENT_FORMAT_PARSER_ARG_NAME_VALUE_SEPARATOR"=boolean)"

FILTERX_GENERATOR_FUNCTION_DECLARE(parse_cef);

FilterXExpr *filterx_function_parse_cef_new(FilterXFunctionArgs *args, GError **error);

#endif

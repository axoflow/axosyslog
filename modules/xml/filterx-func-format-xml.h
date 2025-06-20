/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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
#ifndef FILTERX_FUNC_FORMAT_XML_H_INCLUDED
#define FILTERX_FUNC_FORMAT_XML_H_INCLUDED

#include "filterx/expr-function.h"

FILTERX_FUNCTION_DECLARE(format_xml);

typedef struct FilterXFunctionFormatXML_
{
  FilterXFunction super;
  FilterXExpr *input;
  gboolean (*append_inner_dict)(FilterXObject *key, FilterXObject *dict, gpointer user_data);
} FilterXFunctionFormatXML;

FilterXExpr *filterx_function_format_xml_new(FilterXFunctionArgs *args, GError **error);

#endif

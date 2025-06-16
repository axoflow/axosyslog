/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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


#ifndef FILTERX_ERROR_H_INCLUDED
#define FILTERX_ERROR_H_INCLUDED

#include "filterx/filterx-scope.h"
#include "filterx/filterx-expr.h"
#include "template/eval.h"

typedef struct _FilterXError
{
  const gchar *message;
  FilterXExpr *expr;
  FilterXObject *object;
  gchar *info;
  guint8 free_info:1, falsy:1;
} FilterXError;

void filterx_error_clear(FilterXError *error);
void filterx_error_copy(FilterXError *src, FilterXError *dst);
const gchar *filterx_error_format(FilterXError *error);
EVTTAG *filterx_error_format_tag(FilterXError *error);
EVTTAG *filterx_error_format_location_tag(FilterXError *error);
void filterx_error_set_info(FilterXError *error, gchar *info, gboolean free_info);
void filterx_error_set_values(FilterXError *error, const gchar *message, FilterXExpr *expr, FilterXObject *object);
void filterx_falsy_error_set_values(FilterXError *error, const gchar *message, FilterXExpr *expr,
                                    FilterXObject *object);


#endif

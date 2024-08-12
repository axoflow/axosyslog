/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#ifndef FILTERX_EXPR_VARIABLE_H_INCLUDED
#define FILTERX_EXPR_VARIABLE_H_INCLUDED

#include "filterx/filterx-expr.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-config.h"
#include "filterx/object-string.h"
#include "cfg.h"

FilterXExpr *filterx_msg_variable_expr_new(FilterXObject *name);
FilterXExpr *filterx_floating_variable_expr_new(FilterXObject *name);
void filterx_variable_expr_declare(FilterXExpr *s);

static inline FilterXObject *
filterx_frozen_dollar_msg_varname(GlobalConfig *cfg, const gchar *name)
{
  gchar *dollar_name = g_strdup_printf("$%s", name);
  FilterXObject *dollar_name_obj = filterx_string_new(dollar_name, -1);
  g_free(dollar_name);

  filterx_config_freeze_object(cfg, dollar_name_obj);
  return dollar_name_obj;
}

#endif

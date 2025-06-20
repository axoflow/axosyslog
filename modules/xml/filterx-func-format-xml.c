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
#include "filterx-func-format-xml.h"

static FilterXObject *
_eval(FilterXExpr *s)
{
  return NULL;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  self->input = filterx_expr_optimize(self->input);

  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  if (!filterx_expr_init(self->input, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  filterx_expr_deinit(self->input, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  filterx_expr_unref(self->input);
  filterx_function_free_method(&self->super);
}

static gboolean
_extract_argument(FilterXFunctionFormatXML *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FORMAT_XML_USAGE);
      return FALSE;
    }

  self->input = filterx_function_args_get_expr(args, 0);
  if (!self->input)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "input must be set. " FILTERX_FUNC_FORMAT_XML_USAGE);
      return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_function_format_xml_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFormatXML *self = g_new0(FilterXFunctionFormatXML, 1);
  filterx_function_init_instance(&self->super, "format_xml");

  self->super.super.eval = _eval;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.free_fn = _free;

  if (!_extract_argument(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(format_xml, filterx_function_format_xml_new);
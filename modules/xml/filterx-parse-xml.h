/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef FILTERX_PARSE_XML_H_INCLUDED
#define FILTERX_PARSE_XML_H_INCLUDED

#include "filterx/expr-function.h"

FILTERX_GENERATOR_FUNCTION_DECLARE(parse_xml);

FilterXExpr *filterx_generator_function_parse_xml_new(FilterXFunctionArgs *args, GError **error);


typedef struct FilterXParseXmlState_ FilterXParseXmlState;
struct FilterXParseXmlState_
{
  GQueue *xml_elem_context_stack;

  void (*free_fn)(FilterXParseXmlState *self);
};

void filterx_parse_xml_state_init_instance(FilterXParseXmlState *self);
void filterx_parse_xml_state_free_method(FilterXParseXmlState *self);

static inline void
filterx_parse_xml_state_free(FilterXParseXmlState *self)
{
  self->free_fn(self);
  g_free(self);
}


typedef struct XmlElemContext_
{
  FilterXObject *current_obj;
  FilterXObject *parent_obj;
} XmlElemContext;

XmlElemContext *xml_elem_context_new(FilterXObject *parent_obj, FilterXObject *current_obj);
void xml_elem_context_free(XmlElemContext *self);
void xml_elem_context_set_current_obj(XmlElemContext *self, FilterXObject *current_obj);
void xml_elem_context_set_parent_obj(XmlElemContext *self, FilterXObject *parent_obj);


typedef struct FilterXGeneratorFunctionParseXml_ FilterXGeneratorFunctionParseXml;
struct FilterXGeneratorFunctionParseXml_
{
  FilterXGeneratorFunction super;
  FilterXExpr *xml_expr;

  FilterXParseXmlState *(*create_state)(void);
};

#endif

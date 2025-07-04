/*
 * Copyright (c) 2002-2017 Balabit
 * Copyright (c) 1998-2017 Balázs Scheidler
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

#ifndef APPMODEL_APPLICATION_H_INCLUDED
#define APPMODEL_APPLICATION_H_INCLUDED

#include "appmodel-context.h"
#include "cfg-lexer.h"

#define APPLICATION_TYPE_NAME "application"

typedef struct _Application
{
  AppModelObject super;
  gchar *filter_expr;
  gchar *parser_expr;
  gchar *filterx_expr;
  CFG_LTYPE filter_lloc;
  CFG_LTYPE parser_lloc;
  CFG_LTYPE filterx_lloc;
} Application;

void application_set_filter(Application *self, const gchar *filter_expr, CFG_LTYPE *lloc);
void application_set_parser(Application *self, const gchar *parser_expr, CFG_LTYPE *lloc);
void application_set_filterx(Application *self, const gchar *parser_expr, CFG_LTYPE *lloc);

Application *application_new(const gchar *name, const gchar *topic);

#endif

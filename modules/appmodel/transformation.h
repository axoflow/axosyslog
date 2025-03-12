/*
 * Copyright (c) 2023 Balázs Scheidler <balazs.scheidler@axoflow.com>
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

#ifndef APPMODEL_TRANSFORMATION_H_INCLUDED
#define APPMODEL_TRANSFORMATION_H_INCLUDED

#include "appmodel-context.h"

#define TRANSFORMATION_TYPE_NAME "transformation"

typedef struct _TransformStep
{
  gchar *name;
  gchar *filterx_expr;
  gchar *parser_expr;
} TransformStep;

TransformStep *transform_step_filterx_new(const gchar *name, const gchar *expr);
TransformStep *transform_step_parser_new(const gchar *name, const gchar *expr);
void transform_step_free(TransformStep *self);


typedef struct _Transform
{
  gchar *name;
  GList *steps;
  gboolean filterx_only;
} Transform;

void transform_add_filterx_step(Transform *self, const gchar *name, const gchar *step);
void transform_add_parser_step(Transform *self, const gchar *name, const gchar *step);
Transform *transform_new(const gchar *name);
void transform_free(Transform *self);

typedef struct _Transformation
{
  AppModelObject super;
  GList *transforms;
} Transformation;

void transformation_add_transform(Transformation *self, Transform *transform);
Transformation *transformation_new(const gchar *app, const gchar *name);

#endif

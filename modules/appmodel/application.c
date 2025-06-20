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

#include "application.h"

void
application_set_filter(Application *self, const gchar *filter_expr, CFG_LTYPE *lloc)
{
  g_free(self->filter_expr);
  self->filter_expr = g_strdup(filter_expr);
  if (lloc)
    self->filter_lloc = *lloc;
}

void
application_set_parser(Application *self, const gchar *parser_expr, CFG_LTYPE *lloc)
{
  g_free(self->parser_expr);
  self->parser_expr = g_strdup(parser_expr);
  if (lloc)
    self->parser_lloc = *lloc;
}

void
application_set_filterx(Application *self, const gchar *filterx_expr, CFG_LTYPE *lloc)
{
  g_free(self->filterx_expr);
  self->filterx_expr = g_strdup(filterx_expr);
  if (lloc)
    self->filterx_lloc = *lloc;
}

static void
application_free(AppModelObject *s)
{
  Application *self = (Application *) s;
  g_free(self->filter_expr);
  g_free(self->parser_expr);
  g_free(self->filterx_expr);
}

Application *
application_new(const gchar *name, const gchar *topic)
{
  Application *self = g_new0(Application, 1);

  appmodel_object_init_instance(&self->super, APPLICATION_TYPE_NAME, name, topic);
  self->super.free_fn = application_free;
  return self;
}

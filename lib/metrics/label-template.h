/*
 * Copyright (c) 2023 Attila Szakacs
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

#ifndef LABEL_TEMPLATE_H_INCLUDED
#define LABEL_TEMPLATE_H_INCLUDED

#include "template/templates.h"
#include "stats/stats-cluster.h"

typedef struct _LabelTemplate LabelTemplate;

LabelTemplate *label_template_new(const gchar *name, LogTemplate *value_template);
LabelTemplate *label_template_clone(const LabelTemplate *self);
void label_template_free(LabelTemplate *self);

void label_template_format(LabelTemplate *self, const LogTemplateOptions *template_options,
                           LogMessage *msg, GString *value_buffer, StatsClusterLabel *label);
gint label_template_compare(const LabelTemplate *self, const LabelTemplate *other);

#endif

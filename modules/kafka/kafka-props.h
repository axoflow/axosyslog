/*
 * Copyright (c) 2018 Kokan <kokaipeter@gmail.com>
 * Copyright (c) 2014 Pierre-Yves Ritschard <pyr@spootnik.org>
 * Copyright (c) 2019 Balabit
 * Copyright (c) 2019 Balazs Scheidler
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

#ifndef KAFKA_PROPS_H_INCLUDED
#define KAFKA_PROPS_H_INCLUDED

#include "syslog-ng.h"

typedef struct _KafkaProperty
{
  gchar *name;
  gchar *value;
} KafkaProperty;

KafkaProperty *kafka_property_new(const gchar *name, const gchar *value);
void kafka_property_free(KafkaProperty *self);
void kafka_property_list_free(GList *l);
const gchar *kafka_property_list_find_not_empty(GList *l, const gchar *name);

#endif

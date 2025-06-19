/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
 * Copyright (c) 2023 Axoflow
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
#ifndef CFG_PERSIST_H_INCLUDED
#define CFG_PERSIST_H_INCLUDED 1

#include "syslog-ng.h"

typedef struct _PersistConfig
{
  GHashTable *keys;
} PersistConfig;

void persist_config_add(PersistConfig *self, const gchar *name, gpointer value, GDestroyNotify destroy);
gpointer persist_config_fetch(PersistConfig *cfg, const gchar *name);

PersistConfig *persist_config_new(void);
void persist_config_free(PersistConfig *self);

#endif

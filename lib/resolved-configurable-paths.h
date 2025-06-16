/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Laszlo Budai
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


#ifndef RESOLVED_CONFIGURABLE_PATHS_H_INCLUDED
#define RESOLVED_CONFIGURABLE_PATHS_H_INCLUDED

#include <glib.h>

typedef struct _ResolvedConfigurablePaths
{
  const gchar *cfgfilename;
  const gchar *persist_file;
  const gchar *ctlfilename;
  const gchar *initial_module_path;
} ResolvedConfigurablePaths;

extern ResolvedConfigurablePaths resolved_configurable_paths;
void resolved_configurable_paths_init(ResolvedConfigurablePaths *self);

#endif

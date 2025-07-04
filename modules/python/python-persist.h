/*
 * Copyright (c) 2019 Balabit
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

#ifndef SNG_PYTHON_PERSIST_H_INCLUDED
#define SNG_PYTHON_PERSIST_H_INCLUDED

#include "python-module.h"
#include "python-options.h"
#include "logpipe.h"
#include "stats/stats-cluster-key-builder.h"

typedef struct
{
  PyObject_HEAD
  PersistState *persist_state;
  gchar *persist_name;
} PyPersist;

extern PyTypeObject py_persist_type;

typedef struct
{
  PyObject *generate_persist_name_method;
  PythonOptions *options;
  const gchar *class;
  const gchar *id;
} PythonPersistMembers;

const gchar *python_format_stats_key(LogPipe *p, StatsClusterKeyBuilder *kb, const gchar *module,
                                     PythonPersistMembers *options);
const gchar *python_format_persist_name(const LogPipe *p, const gchar *module, PythonPersistMembers *options);

void py_persist_global_init(void);

#endif

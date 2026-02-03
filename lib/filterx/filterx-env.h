/*
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#ifndef FILTERX_ENV_H_INCLUDED
#define FILTERX_ENV_H_INCLUDED

#include "filterx/filterx-object.h"

typedef struct _FilterXEnvironment
{
  GPtrArray *frozen_objects;
  GHashTable *deduplicated_objects;
  GPtrArray *weak_refs;
} FilterXEnvironment;

void filterx_env_freeze_object(FilterXEnvironment *self, FilterXObject **object);
void filterx_env_move(FilterXEnvironment *target, FilterXEnvironment *source);
void filterx_env_init(FilterXEnvironment *self);
void filterx_env_clear(FilterXEnvironment *self);

#endif

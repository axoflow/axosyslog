/*
 * Copyright (c) 2018 Balabit
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

#ifndef NVHANDLE_DESCRIPTORS_H_INCLUDED
#define NVHANDLE_DESCRIPTORS_H_INCLUDED

#include "syslog-ng.h"

typedef struct _NVHandleDesc NVHandleDesc;
struct _NVHandleDesc
{
  gchar *name;
  guint16 flags;
  guint8 name_len;
};

typedef struct
{
  NVHandleDesc *data;
  guint   len;
  guint   allocated_len;
  GPtrArray *old_buffers;
} NVHandleDescArray;

#define NVHANDLE_DESC_ARRAY_INITIAL_SIZE 256

NVHandleDescArray *nvhandle_desc_array_new(guint reserved_size);
void nvhandle_desc_array_free(NVHandleDescArray *self);
void nvhandle_desc_array_append(NVHandleDescArray *self, NVHandleDesc *desc);
#define nvhandle_desc_array_index(self, i)      (((NVHandleDesc*) (self)->data) [(i)])

/* Does not free *self*. */
void nvhandle_desc_free(NVHandleDesc *self);

#endif

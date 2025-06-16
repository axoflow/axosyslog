/*
 * Copyright (c) 2002-2014 Balabit
 * Copyright (c) 2014 Laszlo Budai
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

#ifndef BOOKMARK_H_INCLUDED
#define BOOKMARK_H_INCLUDED

#include "syslog-ng.h"
#include "persist-state.h"

#define MAX_BOOKMARK_DATA_LENGTH (128)

typedef struct _BookmarkContainer
{
  /* Bookmark structure should be aligned (ie. HPUX-11v2 ia64) */
  gint64 other_state[MAX_BOOKMARK_DATA_LENGTH/sizeof(gint64)];
} BookmarkContainer;

struct _Bookmark
{
  PersistState *persist_state;
  void (*save)(Bookmark *self);
  void (*destroy)(Bookmark *self);
  BookmarkContainer container;
};

static inline void
bookmark_save(Bookmark *self)
{
  if (self->save)
    {
      self->save(self);
    }
}

static inline void
bookmark_destroy(Bookmark *self)
{
  if (self->destroy)
    {
      self->destroy(self);
    }
}

#endif

/*
 * Copyright (c) 2020 One Identity
 * Copyright (c) 2020 László Várady
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

#ifndef _SNG_PYTHON_BOOKMARK_H
#define _SNG_PYTHON_BOOKMARK_H

#include "python-module.h"
#include "ack-tracker/bookmark.h"

typedef struct _PyBookmark
{
  PyObject_HEAD
  PyObject *data;
  PyObject *save;
} PyBookmark;

extern PyTypeObject py_bookmark_type;

int py_is_bookmark(PyObject *obj);
PyBookmark *py_bookmark_new(PyObject *data, PyObject *save);
void py_bookmark_fill(Bookmark *bookmark, PyBookmark *py_bookmark);

PyBookmark *bookmark_to_py_bookmark(Bookmark *bookmark);

void py_bookmark_global_init(void);

#endif

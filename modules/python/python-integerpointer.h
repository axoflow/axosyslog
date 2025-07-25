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

#ifndef _SNG_PYTHON_INTEGERPOINTER_H
#define _SNG_PYTHON_INTEGERPOINTER_H

#include "python-module.h"

typedef struct _PyIntegerPointer
{
  PyObject_HEAD
  gint *ptr;
} PyIntegerPointer;

extern PyTypeObject py_integer_pointer_type;

PyObject *py_integer_pointer_new(gpointer ptr);

void py_integer_pointer_global_init(void);

#endif

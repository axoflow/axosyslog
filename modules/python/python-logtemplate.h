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

#ifndef _SNG_PYTHON_TEMPLATE_H
#define _SNG_PYTHON_TEMPLATE_H

#include "python-module.h"
#include "python-logtemplate-options.h"
#include "template/templates.h"

typedef struct _PyLogTemplate
{
  PyObject_HEAD
  LogTemplate *template;
  PyLogTemplateOptions *py_template_options;
} PyLogTemplate;

extern PyTypeObject py_log_template_type;
extern PyObject *PyExc_LogTemplate;


PyObject *py_log_template_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
void py_log_template_free(PyLogTemplate *self);
PyObject *py_log_template_format(PyObject *s, PyObject *args, PyObject *kwds);

void py_log_template_global_init(void);

#endif

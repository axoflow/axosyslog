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
#include "python-logtemplate.h"
#include "python-logtemplate-options.h"
#include "python-logmsg.h"
#include "python-types.h"
#include "python-main.h"
#include "scratch-buffers.h"
#include "messages.h"

PyTypeObject py_log_template_type;
PyObject *PyExc_LogTemplate;


PyObject *
py_log_template_format(PyObject *s, PyObject *args, PyObject *kwrds)
{
  PyLogTemplate *self = (PyLogTemplate *)s;

  PyLogMessage *msg;
  PyLogTemplateOptions *py_template_options = NULL;
  gint tz = LTZ_SEND;
  gint seqnum = 0;

  static const gchar *kwlist[] = {"msg", "options", "tz", "seqnum", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwrds, "O|Oii", (gchar **)kwlist,
                                   &msg, &py_template_options, &tz, &seqnum))
    return NULL;

  if (!py_is_log_message((PyObject *)msg))
    {
      PyErr_Format(PyExc_TypeError,
                   "LogMessage expected in the first parameter");
      return NULL;
    }

  if (py_template_options && !py_is_log_template_options((PyObject *)py_template_options))
    {
      PyErr_Format(PyExc_TypeError,
                   "LogTemplateOptions expected in the second parameter");
      return NULL;
    }

  LogTemplateOptions *template_options;

  if (py_template_options)
    template_options = &py_template_options->template_options;
  else if (self->py_template_options)
    template_options = &self->py_template_options->template_options;
  else
    template_options = NULL;

  if (!template_options)
    {
      PyErr_Format(PyExc_RuntimeError,
                   "LogTemplateOptions must be provided either in the LogTemplate constructor or as parameter of format");
      return NULL;
    }

  GString *result = scratch_buffers_alloc();
  LogTemplateEvalOptions options = { template_options, tz, seqnum, NULL, LM_VT_STRING };
  log_template_format(self->template, msg->msg, &options, result);

  return py_bytes_from_string(result->str, result->len);
}

int
py_log_template_init(PyObject *s, PyObject *args, PyObject *kwds)
{
  PyLogTemplate *self = (PyLogTemplate *)s;
  const gchar *template_string;
  PyLogTemplateOptions *py_template_options = NULL;
  GlobalConfig *cfg = _py_get_config_from_main_module()->cfg;

  if (!PyArg_ParseTuple(args, "s|O", &template_string, &py_template_options))
    return -1;

  if (py_template_options && !py_is_log_template_options((PyObject *)py_template_options))
    {
      PyErr_Format(PyExc_TypeError,
                   "LogTemplateOptions expected in the second parameter");
      return -1;
    }

  LogTemplate *template = log_template_new(cfg, NULL);
  GError *error = NULL;
  if (!log_template_compile(template, template_string, &error))
    {
      PyErr_Format(PyExc_LogTemplate,
                   "Error compiling template: %s", error->message);
      g_clear_error(&error);
      log_template_unref(template);
      return -1;
    }

  self->template = template;
  self->py_template_options = py_template_options;
  Py_XINCREF(py_template_options);

  return 0;
}

PyObject *
py_log_template_str(PyObject *s)
{
  PyLogTemplate *self = (PyLogTemplate *)s;

  return py_string_from_string(self->template->template_str, -1);
}

void
py_log_template_free(PyLogTemplate *self)
{
  log_template_unref(self->template);
  Py_XDECREF(self->py_template_options);
  Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyMethodDef py_log_template_methods[] =
{
  { "format", (PyCFunction)py_log_template_format, METH_VARARGS | METH_KEYWORDS, "format template" },
  { NULL }
};

PyTypeObject py_log_template_type =
{
  PyVarObject_HEAD_INIT(&PyType_Type, 0)
  .tp_name = "LogTemplate",
  .tp_basicsize = sizeof(PyLogTemplate),
  .tp_dealloc = (destructor) py_log_template_free,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
  .tp_doc = "LogTemplate class encapsulating a syslog-ng template",
  .tp_methods = py_log_template_methods,
  .tp_new = PyType_GenericNew,
  .tp_init = py_log_template_init,
  .tp_str = py_log_template_str,
  0,
};

void
py_log_template_global_init(void)
{
  py_log_template_options_global_init();

  PyType_Ready(&py_log_template_type);
  PyModule_AddObject(PyImport_AddModule("_syslogng"), "LogTemplate", (PyObject *) &py_log_template_type);

  PyExc_LogTemplate = PyErr_NewException("_syslogng.LogTemplateException", NULL, NULL);
  PyModule_AddObject(PyImport_AddModule("_syslogng"), "LogTemplateException", (PyObject *) PyExc_LogTemplate);
}

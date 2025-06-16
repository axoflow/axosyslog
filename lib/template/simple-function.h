/*
 * Copyright (c) 2002-2014 Balabit
 * Copyright (c) 1998-2014 Balázs Scheidler
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

#ifndef TEMPLATE_SIMPLE_FUNCTION_H_INCLUDED
#define TEMPLATE_SIMPLE_FUNCTION_H_INCLUDED

#include "template/function.h"

/* function pointers for template functions */

typedef struct _TFSimpleFuncState
{
  gint argc;
  LogTemplate **argv_templates;
} TFSimpleFuncState;

typedef void (*TFSimpleFunc)(LogMessage *msg, gint argc, GString *argv[], GString *result, LogMessageValueType *type);

gboolean tf_simple_func_prepare(LogTemplateFunction *self, gpointer state, LogTemplate *parent, gint argc,
                                gchar *argv[], GError **error);
void tf_simple_func_eval(LogTemplateFunction *self, gpointer state, LogTemplateInvokeArgs *args);
void tf_simple_func_call(LogTemplateFunction *self, gpointer state, const LogTemplateInvokeArgs *args, GString *result,
                         LogMessageValueType *type);
void tf_simple_func_free_state(gpointer state);

#define TEMPLATE_FUNCTION_SIMPLE(x) TEMPLATE_FUNCTION(TFSimpleFuncState, x, tf_simple_func_prepare, tf_simple_func_eval, tf_simple_func_call, tf_simple_func_free_state, x)

#endif

/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Szilard Parrag
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "func-sdata.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"

#include <string.h>


FilterXObject *
filterx_simple_function_is_sdata_from_enterprise(FilterXExpr *s, GPtrArray *args)
{
  if (args == NULL || args->len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument", FALSE);
      return NULL;
    }

  FilterXObject *obj = filterx_typecast_string(NULL, args);
  if (!obj)
    return NULL;
  const gchar *str_value;
  gsize len;
  if (!filterx_object_extract_string(obj, &str_value, &len))
    return NULL;

  gboolean contains = FALSE;
  FilterXEvalContext *context = filterx_eval_get_context();
  LogMessage *msg = context->msgs[0];

  for (guint8 i = 0; i < msg->num_sdata && !contains; i++)
    {
      const gchar *value = log_msg_get_value_name(msg->sdata[i], NULL);
      gchar *at_sign = strchr(value, '@');
      if (!at_sign)
        continue;
      contains = strncmp(at_sign+1, str_value, len) == 0;
    }
  filterx_object_unref(obj);
  return filterx_boolean_new(contains);
}

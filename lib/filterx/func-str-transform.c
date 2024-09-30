/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/func-str-transform.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"

static const gchar *
_extract_str_arg(FilterXExpr *s, GPtrArray *args, gssize *len)
{
  if (args == NULL || args->len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument", FALSE);
      return NULL;
    }

  const gchar *str;
  gsize inner_len;
  FilterXObject *object = g_ptr_array_index(args, 0);

  if (!filterx_object_extract_string_ref(object, &str, &inner_len))
    {
      filterx_simple_function_argument_error(s, "Object must be string", FALSE);
      return NULL;
    }

  *len = (gssize) MIN(inner_len, G_MAXINT64);
  return str;
}

FilterXObject *
filterx_simple_function_lower(FilterXExpr *s, GPtrArray *args)
{
  gssize len;
  const gchar *str = _extract_str_arg(s, args, &len);
  if (!str)
    return NULL;

  gchar *lower = g_utf8_strdown(str, len);
  FilterXObject *result = filterx_string_new(lower, -1);
  g_free(lower);

  return result;
}

FilterXObject *
filterx_simple_function_upper(FilterXExpr *s, GPtrArray *args)
{
  gssize len;
  const gchar *str = _extract_str_arg(s, args, &len);
  if (!str)
    return NULL;

  gchar *upper = g_utf8_strup(str, len);
  FilterXObject *result = filterx_string_new(upper, -1);
  g_free(upper);

  return result;
}

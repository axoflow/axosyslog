/*
 * Copyright (c) 2024-2025 Axoflow
 * Copyright (c) 2024 Attila Szakacs
 * Copyright (c) 2025 László Várady
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

#include "filterx/func-str-transform.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "str-utils.h"

static const gchar *
_extract_str_arg(FilterXExpr *s, FilterXObject *args[], gsize args_len, gssize *len)
{
  if (args == NULL || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument");
      return NULL;
    }

  const gchar *str;
  gsize inner_len;
  FilterXObject *object = args[0];

  if (!filterx_object_extract_string_ref(object, &str, &inner_len))
    {
      filterx_simple_function_argument_error(s, "Object must be string");
      return NULL;
    }

  *len = (gssize) MIN(inner_len, G_MAXINT64);
  return str;
}

static void
_translate_to_lower(gchar *target, const gchar *source, gsize len)
{
  for (gsize i = 0; i < len; i++)
    {
      target[i] = ch_tolower(source[i]);
    }
}

static void
_translate_to_upper(gchar *target, const gchar *source, gsize len)
{
  for (gsize i = 0; i < len; i++)
    {
      target[i] = ch_toupper(source[i]);
    }
}

FilterXObject *
filterx_simple_function_lower(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  gssize len;
  const gchar *str = _extract_str_arg(s, args, args_len, &len);
  if (!str)
    return NULL;

  FilterXObject *result = filterx_string_new_translated(str, len, _translate_to_lower);
  return result;
}

FilterXObject *
filterx_simple_function_upper(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  gssize len;
  const gchar *str = _extract_str_arg(s, args, args_len, &len);
  if (!str)
    return NULL;

  FilterXObject *result = filterx_string_new_translated(str, len, _translate_to_upper);
  return result;
}

FilterXObject *
filterx_simple_function_str_replace(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args == NULL || (args_len != 3 && args_len != 4))
    {
      filterx_simple_function_argument_error(s, "Requires 3 or 4 arguments");
      return NULL;
    }

  FilterXObject *object = args[0];
  FilterXObject *old = args[1];
  FilterXObject *new = args[2];
  FilterXObject *limit = NULL;

  if (args_len == 4)
    limit = args[3];

  const gchar *str;
  gsize len;
  if (!filterx_object_extract_string_ref(object, &str, &len))
    {
      filterx_simple_function_argument_error(s, "First argument must be string");
      return NULL;
    }

  const gchar *old_str;
  if (!filterx_object_extract_string_as_cstr(old, &old_str))
    {
      filterx_simple_function_argument_error(s, "Second argument must be string");
      return NULL;
    }

  const gchar *new_str;
  if (!filterx_object_extract_string_as_cstr(new, &new_str))
    {
      filterx_simple_function_argument_error(s, "Third argument must be string");
      return NULL;
    }

  gint64 limit_num = 0;
  if (limit && !filterx_object_extract_integer(limit, &limit_num))
    {
      filterx_simple_function_argument_error(s, "Fourth argument must be integer");
      return NULL;
    }

  GString *replaced = g_string_new_len(str, len);

  g_string_replace(replaced, old_str, new_str, limit_num);
  FilterXObject *result = filterx_string_new_take(replaced->str, (gssize) replaced->len);

  g_string_free(replaced, FALSE);

  return result;
}

static inline gchar *
_lrstrip(gchar *str)
{
  /* g_strstrip() is a macro */
  return g_strstrip(str);
}

static inline FilterXObject *
_str_strip(FilterXExpr *s, FilterXObject *args[], gsize args_len, gchar *(*transform)(gchar *str))
{
  gssize len;
  const gchar *str = _extract_str_arg(s, args, args_len, &len);
  if (!str)
    return NULL;

  gchar *trimmed = g_strndup(str, len);
  trimmed = transform(trimmed);

  FilterXObject *result = filterx_string_new_take(trimmed, (gssize) strlen(trimmed));
  return result;
}

FilterXObject *
filterx_simple_function_str_strip(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  return _str_strip(s, args, args_len, _lrstrip);
}

FilterXObject *
filterx_simple_function_str_lstrip(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  return _str_strip(s, args, args_len, g_strchug);
}

FilterXObject *
filterx_simple_function_str_rstrip(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  return _str_strip(s, args, args_len, g_strchomp);
}

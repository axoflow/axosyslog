/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/func-encode.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"

/* base64 */

FilterXObject *
filterx_simple_function_base64_encode(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args == NULL || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument");
      return NULL;
    }

  const gchar *input;
  gsize input_len;

  if (!filterx_object_extract_string_ref(args[0], &input, &input_len) &&
      !filterx_object_extract_bytes_ref(args[0], &input, &input_len))
    {
      filterx_simple_function_argument_error(s, "Argument must be a string or bytes");
      return NULL;
    }

  gchar *encoded = g_base64_encode((const guchar *) input, input_len);
  return filterx_string_new_take(encoded, (gssize) strlen(encoded));
}

FilterXObject *
filterx_simple_function_base64_decode(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args == NULL || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument");
      return NULL;
    }

  const gchar *input;
  gsize input_len;

  if (!filterx_object_extract_string_ref(args[0], &input, &input_len))
    {
      filterx_simple_function_argument_error(s, "Argument must be a string");
      return NULL;
    }

  guchar *decoded = g_malloc(input_len / 4 * 3 + 3);
  gsize decoded_len = 0;
  gint state = 0;
  guint save = 0;
  decoded_len = g_base64_decode_step(input, input_len, decoded, &state, &save);
  return filterx_bytes_new_take((gchar *) decoded, (gssize) decoded_len);
}

/* urlencode */

FilterXObject *
filterx_simple_function_urlencode(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args == NULL || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument");
      return NULL;
    }

  const gchar *input;

  if (!filterx_object_extract_string_as_cstr(args[0], &input))
    {
      filterx_simple_function_argument_error(s, "Argument must be a string");
      return NULL;
    }

  gchar *encoded = g_uri_escape_string(input, NULL, FALSE);
  return filterx_string_new_take(encoded, (gssize) strlen(encoded));
}

FilterXObject *
filterx_simple_function_urldecode(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args == NULL || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument");
      return NULL;
    }

  const gchar *input;

  if (!filterx_object_extract_string_as_cstr(args[0], &input))
    {
      filterx_simple_function_argument_error(s, "Argument must be a string");
      return NULL;
    }

  gchar *decoded = g_uri_unescape_string(input, NULL);
  if (!decoded)
    {
      filterx_eval_push_error_info_printf("Failed to decode URL-encoded string", s,
                                          "invalid URL encoding: %s", input);
      return NULL;
    }

  return filterx_string_new_take(decoded, (gssize) strlen(decoded));
}


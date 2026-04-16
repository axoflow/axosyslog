/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/func-glob.h"
#include "filterx/object-primitive.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-sequence.h"
#include "filterx/filterx-eval.h"
#include "str-utils.h"
#include <fnmatch.h>

#define FILTERX_FUNC_GLOB_MATCH_USAGE "Usage: glob_match(string, patterns)"

static FilterXObject *
_match_pattern(FilterXExpr *s, const gchar *filename, FilterXObject *pattern_obj)
{
  const gchar *pattern;
  if (!filterx_object_extract_string_as_cstr(pattern_obj, &pattern))
    {
      filterx_simple_function_argument_error(s, "Patterns must be strings. " FILTERX_FUNC_GLOB_MATCH_USAGE);
      return NULL;
    }

  gboolean matched = (fnmatch(pattern, filename, FNM_PATHNAME | FNM_PERIOD) == 0);
  return filterx_boolean_new(matched);
}

FilterXObject *
filterx_simple_function_glob_match(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args_len != 2)
    {
      filterx_simple_function_argument_error(s, "Requires exactly two arguments. " FILTERX_FUNC_GLOB_MATCH_USAGE);
      return NULL;
    }

  const gchar *filename;
  if (!filterx_object_extract_string_as_cstr(args[0], &filename))
    {
      filterx_simple_function_argument_error(s, "First argument must be a string. " FILTERX_FUNC_GLOB_MATCH_USAGE);
      return NULL;
    }

  FilterXObject *patterns = args[1];
  if (!filterx_object_is_type_or_ref(patterns, &FILTERX_TYPE_NAME(sequence)))
    {
      /* not a sequence, handle it as a pattern */
      return _match_pattern(s, filename, patterns);
    }

  guint64 len = 0;
  filterx_object_len(patterns, &len);
  for (guint64 i = 0; i < len; i++)
    {
      FilterXObject *pattern = filterx_sequence_get_subscript(patterns, (gint64) i);
      if (!pattern)
        {
          filterx_simple_function_argument_error(s, "Failed to get pattern from list. " FILTERX_FUNC_GLOB_MATCH_USAGE);
          return NULL;
        }

      FilterXObject *result = _match_pattern(s, filename, pattern);
      filterx_object_unref(pattern);

      if (!result || filterx_object_truthy(result))
        return result;
    }

  return filterx_boolean_new(FALSE);
}

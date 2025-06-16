/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/filterx-error.h"
#include "scratch-buffers.h"

void
filterx_error_clear(FilterXError *error)
{
  filterx_object_unref(error->object);
  if (error->free_info)
    g_free(error->info);
  memset(error, 0, sizeof(*error));
}

const gchar *
filterx_error_format(FilterXError *error)
{
  if (!error->message)
    return NULL;

  GString *error_buffer = scratch_buffers_alloc();
  const gchar *extra_info = NULL;

  if (error->info)
    {
      extra_info = error->info;
    }
  else if (error->object)
    {
      GString *buf = scratch_buffers_alloc();

      if (!filterx_object_repr(error->object, buf))
        {
          LogMessageValueType t;
          if (!filterx_object_marshal(error->object, buf, &t))
            g_assert_not_reached();
        }
      extra_info = buf->str;
    }

  g_string_assign(error_buffer, error->message);
  if (extra_info)
    {
      g_string_append_len(error_buffer, ": ", 2);
      g_string_append(error_buffer, extra_info);
    }
  /* the caller does not need to free str, it is a scratch buffer */
  return error_buffer->str;
}

EVTTAG *
filterx_error_format_tag(FilterXError *error)
{
  const gchar *formatted_error = filterx_error_format(error);
  if (!formatted_error)
    formatted_error = "Error information unset";
  return evt_tag_str("error", formatted_error);
}

EVTTAG *
filterx_error_format_location_tag(FilterXError *error)
{
  return filterx_expr_format_location_tag(error->expr);
}

void
filterx_error_set_values(FilterXError *error, const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  error->message = message;
  error->expr = expr;
  error->object = filterx_object_ref(object);
}

void
filterx_falsy_error_set_values(FilterXError *error, const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  error->message = message;
  error->expr = expr;
  error->object = filterx_object_ref(object);
  error->falsy = TRUE;
}

void
filterx_error_set_info(FilterXError *error, gchar *info, gboolean free_info)
{
  error->info = info;
  error->free_info = free_info;
}

void
filterx_error_copy(FilterXError *src, FilterXError *dst)
{
  g_assert(!dst->message);

  dst->message = src->message;
  dst->expr = src->expr;
  dst->object = filterx_object_ref(src->object);
  dst->info = g_strdup(src->info);
  dst->free_info = TRUE;
}

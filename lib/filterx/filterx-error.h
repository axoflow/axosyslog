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


#ifndef FILTERX_ERROR_H_INCLUDED
#define FILTERX_ERROR_H_INCLUDED

#include "filterx/filterx-scope.h"
#include "filterx/filterx-expr.h"

typedef struct _FilterXError
{
  const gchar *message;
  FilterXExpr *expr;
  FilterXObject *object;
  gchar *info;
  gboolean free_info;
  guint8 falsy:1;
} FilterXError;

const gchar *filterx_error_format(FilterXError *error);
EVTTAG *filterx_error_format_tag(FilterXError *error);
EVTTAG *filterx_error_format_location_tag(FilterXError *error);

static inline void
filterx_error_set_expr(FilterXError *error, FilterXExpr *expr)
{
  error->expr = expr;
}

static inline void
filterx_error_set_values(FilterXError *error, const gchar *message, FilterXObject *object)
{
  error->message = message;
  error->object = filterx_object_ref(object);
}

static inline void
filterx_falsy_error_set_values(FilterXError *error, const gchar *message, FilterXObject *object)
{
  error->message = message;
  error->object = filterx_object_ref(object);
  error->falsy = TRUE;
}

static inline void
filterx_error_set_static_info(FilterXError *error, const gchar *info)
{
  error->info = (gchar *) info;
  error->free_info = FALSE;
}

static inline void G_GNUC_PRINTF(2, 0)
filterx_error_set_vinfo(FilterXError *error, const gchar *fmt, va_list args)
{
  error->info = g_strdup_vprintf(fmt, args);
  error->free_info = TRUE;
}

static inline void G_GNUC_PRINTF(2, 3)
filterx_error_set_info_printf(FilterXError *error, const gchar *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  filterx_error_set_vinfo(error, fmt, args);
  va_end(args);
}

static inline void
filterx_error_copy(FilterXError *src, FilterXError *dst)
{
  g_assert(!dst->message);

  dst->message = src->message;
  dst->expr = src->expr;
  dst->object = filterx_object_ref(src->object);

  dst->info = g_strdup(src->info);
  dst->free_info = TRUE;
}

static inline void
filterx_error_clear(FilterXError *error)
{
  filterx_object_unref(error->object);
  if (error->free_info)
    g_free(error->info);
  memset(error, 0, sizeof(*error));
}


#endif

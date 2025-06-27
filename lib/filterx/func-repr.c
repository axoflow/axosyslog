/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "filterx/func-repr.h"
#include "filterx/object-string.h"
#include "filterx/filterx-globals.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"

FilterXObject *
filterx_simple_function_repr(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  GString *buf = scratch_buffers_alloc();
  if (!filterx_object_repr(object, buf))
    {
      gchar type_name_buf[FILTERX_OBJECT_TYPE_NAME_BUF_SIZE];
      filterx_eval_push_error_info_printf("Failed to convert object to string", s,
                                          "repr() failed on object of type: %s",
                                          filterx_object_format_type_name(object, type_name_buf));
      return NULL;
    }

  return filterx_string_new(buf->str, buf->len);
}

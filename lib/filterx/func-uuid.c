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

#include "filterx/func-uuid.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "filterx/expr-function.h"
#include "uuid.h"

FilterXObject *
filterx_simple_function_uuid(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args_len != 0)
    {
      filterx_simple_function_argument_error(s, "Requires no arguments");
      return NULL;
    }

  gchar uuid_str[37];
  uuid_gen_random(uuid_str, sizeof(uuid_str));
  return filterx_string_new(uuid_str, 36);
}

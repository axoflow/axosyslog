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

#include "template/escaping.h"
#include "str-format.h"

#include <string.h>

void
log_template_default_escape_method(GString *result, const gchar *sstr, gsize len)
{
  gsize i;
  const guchar *ustr = (const guchar *) sstr;

  for (i = 0; i < len; i++)
    {
      if (ustr[i] == '\'' || ustr[i] == '"' || ustr[i] == '\\')
        {
          g_string_append_c(result, '\\');
          g_string_append_c(result, ustr[i]);
        }
      else if (ustr[i] < ' ')
        {
          g_string_append_c(result, '\\');
          format_uint32_padded(result, 3, '0', 8, ustr[i]);
        }
      else
        g_string_append_c(result, ustr[i]);
    }
}

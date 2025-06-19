/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 Balázs Scheidler
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

#include <string.h>
#include <ctype.h>

static void
tf_basename(LogMessage *msg, gint argc, GString *argv[], GString *result, LogMessageValueType *type)
{
  gchar *base;

  *type = LM_VT_STRING;
  base = g_path_get_basename(argv[0]->str);
  g_string_append(result, base);
  g_free(base);
}

TEMPLATE_FUNCTION_SIMPLE(tf_basename);

static void
tf_dirname(LogMessage *msg, gint argc, GString *argv[], GString *result, LogMessageValueType *type)
{
  gchar *dir;

  *type = LM_VT_STRING;
  dir = g_path_get_dirname(argv[0]->str);
  g_string_append(result, dir);
  g_free(dir);
}

TEMPLATE_FUNCTION_SIMPLE(tf_dirname);

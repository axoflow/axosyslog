/*
 * Copyright (c) 2019 Balabit
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

#include "verbose.h"
#include <strings.h>

static gchar *verbose_set = NULL;

GOptionEntry verbose_options[] =
{
  {
    "set", 's', 0, G_OPTION_ARG_STRING, &verbose_set,
    "enable/disable messages", "<on|off|0|1>"
  },
  { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL }
};

gint
slng_verbose(int argc, char *argv[], const gchar *mode, GOptionContext *ctx)
{
  gchar buff[256];

  if (!verbose_set)
    g_snprintf(buff, 255, "LOG %s\n", mode);
  else
    g_snprintf(buff, 255, "LOG %s %s\n", mode,
               strncasecmp(verbose_set, "on", 2) == 0 || verbose_set[0] == '1' ? "ON" : "OFF");

  gchar *command = g_ascii_strup(buff, -1);

  gint ret = dispatch_command(command);

  g_free(command);

  return ret;
}

/*
 * Copyright (c) 2002-2010 Balabit
 * Copyright (c) 1998-2010 Balázs Scheidler
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

#include "syslog-ng.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int
main(int argc, char *argv[])
{
#ifdef ENV_LD_LIBRARY_PATH
  {
    gchar *cur_ldlibpath;
    gchar ldlibpath[512];
#if _AIX
    const gchar *ldlibpath_name = "LIBPATH";
#else
    const gchar *ldlibpath_name = "LD_LIBRARY_PATH";
#endif

    cur_ldlibpath = getenv(ldlibpath_name);
    snprintf(ldlibpath, sizeof(ldlibpath), "%s%s%s", ENV_LD_LIBRARY_PATH, cur_ldlibpath ? ":" : "", cur_ldlibpath ? cur_ldlibpath : "");
    setenv(ldlibpath_name, ldlibpath, TRUE);
  }
#endif
  execv(PATH_SYSLOGNG, argv);
  fprintf(stderr, "Unable to execute main syslog-ng binary from env-wrapper, path=%s, error=%s\n", PATH_SYSLOGNG,
          strerror(errno));
  return 127;
}

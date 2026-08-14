/*
 * Copyright (c) 2018 Balabit
 * Copyright (c) 2018 László Várady <laszlo.varady@balabit.com>
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

#include "http-source.h"
#include "socket/socket-options-inet.h"

#include <strings.h>

gboolean
ehttp_sd_set_mode(LogDriver *d, const gchar *mode)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) d;

  if (strcasecmp(mode, "single") == 0)
    self->mode = EHTTP_SINGLE;
  else if (strcasecmp(mode, "line-separated") == 0 || strcasecmp(mode, "jsonl") == 0)
    self->mode = EHTTP_LINE_SEPARATED;
  else if (strcasecmp(mode, "json-array") == 0)
    self->mode = EHTTP_JSON_ARRAY;
  else
    return FALSE;

  return TRUE;
}

EHTTPSourceDriver *
ehttp_sd_new(GlobalConfig *cfg)
{
  EHTTPSourceDriver *self = g_new0(EHTTPSourceDriver, 1);
  http_sd_init_instance(&self->super, socket_options_inet_new(), http_transport_mapper_new(), cfg);

  self->mode = EHTTP_SINGLE;

  return self;
}

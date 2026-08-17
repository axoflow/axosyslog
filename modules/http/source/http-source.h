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

#ifndef HTTP_SOURCE_H
#define HTTP_SOURCE_H

#include "syslog-ng.h"
#include "http/source/http-source.h"
#include "template/templates.h"

typedef struct EHTTPSourceDriver EHTTPSourceDriver;
typedef struct EHTTPSourceConnection EHTTPSourceConnection;
typedef enum _EHTTPSourceMode
{
  EHTTP_SINGLE,
  EHTTP_LINE_SEPARATED,
  EHTTP_JSON
} EHTTPSourceMode;

struct EHTTPSourceConnection
{
  HTTPSourceConnection super;
  LogMessage *first_message;
};

struct EHTTPSourceDriver
{
  HTTPSourceDriver super;
  EHTTPSourceMode mode;
  gchar *auth_token;
  LogTemplate *response_body;
};

EHTTPSourceDriver *ehttp_sd_new(GlobalConfig *cfg);
gboolean ehttp_sd_set_mode(LogDriver *d, const gchar *mode);
void ehttp_sd_set_auth_token(LogDriver *d, const gchar *auth_token);
void ehttp_sd_set_response_body(LogDriver *d, LogTemplate *response_body);

#endif

/*
 * Copyright (c) 2002-2019 One Identity
 * Copyright (c) 2019 Laszlo Budai <laszlo.budai@oneidentity.com>
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

#ifndef SIGNAL_SLOT_CONNECTOR_HTTP_SIGNALS_H_INCLUDED
#define SIGNAL_SLOT_CONNECTOR_HTTP_SIGNALS_H_INCLUDED

#include "signal-slot-connector/signal-slot-connector.h"
#include "list-adt.h"

typedef struct _HttpHeaderRequestSignalData HttpHeaderRequestSignalData;
typedef struct _HttpResponseReceivedSignalData HttpResponseReceivedSignalData;

typedef enum
{
  HTTP_SLOT_SUCCESS,
  HTTP_SLOT_RESOLVED,
  HTTP_SLOT_CRITICAL_ERROR,
  HTTP_SLOT_PLUGIN_ERROR,
} HttpSlotResultType;

struct _HttpHeaderRequestSignalData
{
  HttpSlotResultType result;
  List *request_headers;
  GString *request_body;
};

struct _HttpResponseReceivedSignalData
{
  HttpSlotResultType result;
  glong http_code;
};

#define signal_http_header_request SIGNAL(http, header_request, HttpHeaderRequestSignalData *)

#define signal_http_response_received SIGNAL(http, response_received, HttpResponseReceivedSignalData *)

#endif

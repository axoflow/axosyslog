/*
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "http-adapter.h"

#define HTTP_ADAPTER_PLUGIN "http-adapter"

static void
_on_http_response_received(HttpAdapter *self, HttpResponseSignalData *data)
{
  http_adapter_adapt_response(self, data);
}

static gboolean
_attach(LogDriverPlugin *s, LogDriver *driver)
{
  HttpAdapter *self = (HttpAdapter *) s;

  SignalSlotConnector *ssc = driver->signal_slot_connector;
  CONNECT(ssc, signal_http_response, _on_http_response_received, self);

  return TRUE;
}

static void
_detach(LogDriverPlugin *s, LogDriver *driver)
{
  HttpAdapter *self = (HttpAdapter *) s;
  SignalSlotConnector *ssc = driver->signal_slot_connector;

  DISCONNECT(ssc, signal_http_response, _on_http_response_received, self);
}

void
http_adapter_init_instance(HttpAdapter *self)
{
  log_driver_plugin_init_instance(&(self->super), HTTP_ADAPTER_PLUGIN);
  self->super.attach = _attach;
  self->super.detach = _detach;
}

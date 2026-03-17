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

#ifndef HTTP_ADAPTER_H_INCLUDED
#define HTTP_ADAPTER_H_INCLUDED 1

#include "driver.h"
#include "modules/http/http-signals.h"

typedef struct _HttpAdapter HttpAdapter;

struct _HttpAdapter
{
  LogDriverPlugin super;
  void (*adapt_response)(HttpAdapter *self, HttpResponseSignalData *data);
};

static inline void
http_adapter_adapt_response(HttpAdapter *self, HttpResponseSignalData *data)
{
  self->adapt_response(self, data);
}

static inline void
http_adapter_free(HttpAdapter *self)
{
  log_driver_plugin_free(&self->super);
}

void http_adapter_init_instance(HttpAdapter *self);

#endif

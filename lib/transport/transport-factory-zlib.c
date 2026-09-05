/*
 * Copyright (c) 2026 Axoflow
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

#include "transport/transport-factory-zlib.h"
#include "transport/transport-zlib.h"

static LogTransport *
_construct_transport(const LogTransportFactory *s, LogTransportStack *stack)
{
  LogTransportFactoryZlib *self = (LogTransportFactoryZlib *) s;

  /* the stack constructs a layer before it makes it active, so a switch to
   * ZLIB after a STARTTLS finds the TLS transport here */
  return log_transport_zlib_new(stack->active_transport, self->level);
}

LogTransportFactory *
transport_factory_zlib_new(gint level)
{
  LogTransportFactoryZlib *instance = g_new0(LogTransportFactoryZlib, 1);

  log_transport_factory_init_instance(&instance->super, LOG_TRANSPORT_ZLIB);
  instance->level = level;
  instance->super.construct_transport = _construct_transport;

  return &instance->super;
}

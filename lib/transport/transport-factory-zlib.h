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

#ifndef TRANSPORT_FACTORY_ZLIB_H_INCLUDED
#define TRANSPORT_FACTORY_ZLIB_H_INCLUDED

#include "transport/transport-stack.h"

typedef struct _LogTransportFactoryZlib LogTransportFactoryZlib;

struct _LogTransportFactoryZlib
{
  LogTransportFactory super;
  gint level;
};

/* The factory of the LOG_TRANSPORT_ZLIB layer.  It wraps whichever transport is
 * active on the stack when the layer is first constructed, so a protocol that
 * switches to it after a TLS handshake gets compression inside TLS.
 *
 * @level is the deflate compression level of the direction we write; what the
 * peer picks for its own direction is its own business.
 */
LogTransportFactory *transport_factory_zlib_new(gint level);

#endif

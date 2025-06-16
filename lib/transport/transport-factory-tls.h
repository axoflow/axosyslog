/*
 * Copyright (c) 2002-2018 Balabit
 * Copyright (c) 2018 Laszlo Budai <laszlo.budai@balabit.com>
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

#ifndef TRANSPORT_FACTORY_TLS_H_INCLUDED
#define TRANSPORT_FACTORY_TLS_H_INCLUDED

#include "transport/transport-stack.h"
#include "transport/tls-context.h"

typedef struct _LogTransportFactoryTLS LogTransportFactoryTLS;

struct _LogTransportFactoryTLS
{
  LogTransportFactory super;
  TLSContext *tls_context;
  TLSVerifier *tls_verifier;
};

LogTransportFactory *transport_factory_tls_new(TLSContext *ctx, TLSVerifier *tls_verifier);

#endif

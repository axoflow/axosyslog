/*
 * Copyright (c) 2018 Balabit
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

#include "http-source-grammar-internal.h"
#include "cfg-grammar-internal.h"

SocketOptions *last_http_sock_options;
TransportMapper *last_http_transport_mapper;
TLSContext *last_http_tls_context;

void
http_source_grammar_set_source_driver(HTTPSourceDriver *sd)
{
  last_driver = &sd->super.super.super;

  last_reader_options = &sd->super.reader_options;
  last_http_sock_options = sd->super.socket_options;
  last_http_transport_mapper = sd->super.transport_mapper;
  last_proto_server_options = &last_reader_options->proto_options.super;
}

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

#ifndef TRANSPORT_ZLIB_H_INCLUDED
#define TRANSPORT_ZLIB_H_INCLUDED

#include "transport/transport-adapter.h"

/* A LogTransportAdapter that compresses everything written to the transport of
 * @base_index and decompresses everything read from it.  Each direction is one
 * continuous zlib stream (RFC 1950) for the life of the transport, and every
 * write ends in a Z_SYNC_FLUSH so that the peer can decode it on arrival.
 *
 * write() reports the input consumed only once every compressed octet of it
 * reached the base transport; while some are still pending it fails with
 * EAGAIN, asks for writability through cond, and the caller MUST retry with
 * the same buffer prefix -- a longer buffer is fine, a shorter one is an
 * error.  Nothing of the retried prefix is compressed twice.
 *
 * read() inflates into the caller's buffer; what does not fit stays in the
 * adapter and is reported by has_pending_input().
 *
 * @level is the deflate compression level: 0 to 9, or Z_DEFAULT_COMPRESSION.
 */
LogTransport *log_transport_zlib_new(LogTransportIndex base_index, gint level);

#endif

#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import logging
import threading

import pyarrow as pa
import pyarrow.flight as flight
from tenacity import retry
from tenacity import stop_after_delay
from tenacity import wait_fixed

logger = logging.getLogger(__name__)


class _FlightServer(flight.FlightServerBase):
    def __init__(self, location):
        super().__init__(location)
        self._tables = {}
        self._stream_counts = {}
        self._batch_counts = {}
        self._lock = threading.Lock()
        self._close_stream_after_next_chunk = False

    def list_flights(self, context, criteria):
        return iter([])

    def do_put(self, context, descriptor, reader, writer):
        key = descriptor.path[0].decode() if descriptor.path else "default"
        with self._lock:
            self._stream_counts[key] = self._stream_counts.get(key, 0) + 1
        for chunk in reader:
            new_table = pa.Table.from_batches([chunk.data])
            with self._lock:
                existing = self._tables.get(key)
                self._tables[key] = pa.concat_tables([existing, new_table]) if existing is not None else new_table
                self._batch_counts[key] = self._batch_counts.get(key, 0) + 1
            writer.write(b"")
            if self._close_stream_after_next_chunk:
                self._close_stream_after_next_chunk = False
                raise flight.FlightUnavailableError("graceful server shutdown")

    def request_close_stream_after_next_chunk(self):
        self._close_stream_after_next_chunk = True

    def get_table(self, key):
        with self._lock:
            return self._tables.get(key)

    def get_stream_count(self, key):
        with self._lock:
            return self._stream_counts.get(key, 0)

    def get_batch_count(self, key):
        with self._lock:
            return self._batch_counts.get(key, 0)


class ArrowFlightIO():
    def __init__(self, port: int, host: str = "0.0.0.0") -> None:
        self.__port = port
        self.__host = host
        self.__server = None
        self.__thread = None

    def start_listener(self) -> None:
        location = f"grpc://{self.__host}:{self.__port}"
        self.__server = _FlightServer(location)
        self.__thread = threading.Thread(target=self.__server.serve, daemon=True)
        self.__thread.start()
        self.__wait_for_server_start()
        logger.info("Arrow Flight server started on port %d", self.__port)

    def stop_listener(self) -> None:
        if self.__server is None:
            return
        self.__server.shutdown()
        self.__thread.join(timeout=5)
        self.__server = None
        self.__thread = None
        logger.info("Arrow Flight server stopped")

    def close_stream_after_next_request(self) -> None:
        if self.__server is None:
            return
        self.__server.request_close_stream_after_next_chunk()

    @retry(wait=wait_fixed(0.1), reraise=True, stop=stop_after_delay(10))
    def __wait_for_server_start(self):
        client = flight.connect(f"grpc://localhost:{self.__port}")
        list(client.list_flights())

    def read_logs(self, path: str) -> list:
        table = self.__server.get_table(path)
        if table is None:
            return []
        col_dict = table.to_pydict()
        return [dict(zip(col_dict.keys(), vals)) for vals in zip(*col_dict.values())]

    def read_log(self, path: str) -> dict:
        return self.read_logs(path)[0]

    def get_row_count(self, path: str) -> int:
        table = self.__server.get_table(path)
        return table.num_rows if table is not None else 0

    def get_stream_count(self, path: str) -> int:
        return self.__server.get_stream_count(path)

    def get_batch_count(self, path: str) -> int:
        return self.__server.get_batch_count(path)

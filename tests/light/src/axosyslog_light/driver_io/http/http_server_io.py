#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
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
import queue
import threading
import time
from http.server import BaseHTTPRequestHandler
from http.server import HTTPServer

from axosyslog_light.common.blocking import DEFAULT_TIMEOUT


class HttpServerIO():
    def __init__(self, port: int, response_code: int = 200) -> None:
        self._port = port
        self._response_code = response_code
        self._queue = queue.Queue()
        self._server = None
        self._thread = None

    def start_listener(self):
        q = self._queue
        response_code = self._response_code

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                content_length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(content_length)
                q.put(body.decode("utf-8"))
                self.send_response(response_code)
                self.send_header("Connection", "close")
                self.end_headers()
                self.close_connection = True

            def do_PUT(self):
                self.do_POST()

            def log_message(self, format, *args):
                pass

        self._server = HTTPServer(("127.0.0.1", self._port), Handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()

    def stop_listener(self):
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
            self._thread.join(timeout=5.0)
            self._server = None
            self._thread = None

    def read_number_of_messages(self, counter: int, timeout: float = DEFAULT_TIMEOUT):
        messages = []
        deadline = time.monotonic() + timeout
        while len(messages) < counter:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise Exception("Timeout waiting for {} messages, received {}".format(counter, len(messages)))
            try:
                msg = self._queue.get(timeout=min(remaining, 0.1))
                messages.append(msg)
            except queue.Empty:
                pass
        return messages

    def read_until_messages(self, expected_messages, timeout: float = DEFAULT_TIMEOUT):
        received = []
        to_find = list(expected_messages)
        deadline = time.monotonic() + timeout
        while to_find:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise Exception("Timeout waiting for messages. Still missing: {}".format(to_find))
            try:
                msg = self._queue.get(timeout=min(remaining, 0.1))
                received.append(msg)
                for expected in list(to_find):
                    if expected in msg:
                        to_find.remove(expected)
            except queue.Empty:
                pass
        return received

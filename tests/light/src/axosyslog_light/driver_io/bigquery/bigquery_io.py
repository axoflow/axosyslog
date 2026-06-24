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
from concurrent import futures

import grpc
from google.cloud.bigquery_storage_v1.types import storage
from google.cloud.bigquery_storage_v1.types import stream
from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool
from google.protobuf.message_factory import GetMessageClass

logger = logging.getLogger(__name__)

_SERVICE = "google.cloud.bigquery.storage.v1.BigQueryWrite"


def _table_of(write_stream_name):
    # "projects/P/datasets/D/tables/T/streams/X" -> "projects/P/datasets/D/tables/T"
    return write_stream_name.split("/streams/")[0]


class _RowDecoder:
    # The client sends the proto descriptor in every AppendRows request, then encodes each
    # row against it. We rebuild a message class from that descriptor and parse the rows back
    # into dicts, so tests can assert on the payload and not just on counts.
    _seq = 0

    def __init__(self, proto_descriptor):
        pool = descriptor_pool.DescriptorPool()
        file_proto = descriptor_pb2.FileDescriptorProto()
        _RowDecoder._seq += 1
        file_proto.name = "bigquery_mock_{}.proto".format(_RowDecoder._seq)
        file_proto.message_type.add().CopyFrom(proto_descriptor)
        pool.Add(file_proto)
        self._message_class = GetMessageClass(pool.FindMessageTypeByName(proto_descriptor.name))

    def decode(self, serialized_row):
        message = self._message_class()
        message.ParseFromString(serialized_row)
        return {field.name: getattr(message, field.name) for field in message.DESCRIPTOR.fields}


class _BigQueryWriteServicer:
    def __init__(self):
        self._lock = threading.Lock()
        self._rows = {}
        self._row_counts = {}
        self._batch_counts = {}
        self._stream_counts = {}
        self._create_counts = {}
        self._finalize_counts = {}
        self._stream_seq = 0
        self._restart_stream_once = False
        self._error = None
        self._decoder = None
        self._decoder_key = None

    def create_write_stream(self, request, context):
        with self._lock:
            self._create_counts[request.parent] = self._create_counts.get(request.parent, 0) + 1
            self._stream_seq += 1
            name = "{}/streams/_mock{}".format(request.parent, self._stream_seq)
        return stream.WriteStream(name=name)

    def append_rows(self, request_iterator, context):
        table = None
        for request in request_iterator:
            if request.write_stream:
                table = _table_of(request.write_stream)
            serialized_rows = request.proto_rows.rows.serialized_rows
            rows = self._decode_rows(request, serialized_rows)
            with self._lock:
                if self._error is not None:
                    code, message = self._error
                    context.abort(code, message)
                self._rows.setdefault(table, []).extend(rows)
                self._row_counts[table] = self._row_counts.get(table, 0) + len(serialized_rows)
                self._batch_counts[table] = self._batch_counts.get(table, 0) + 1
            yield storage.AppendRowsResponse(append_result=storage.AppendRowsResponse.AppendResult())
            with self._lock:
                abort = self._restart_stream_once
                self._restart_stream_once = False
            if abort:
                # Simulate the server-side AppendRows stream restart described in #957:
                # the stream is torn down with a retryable status after a batch was acked.
                context.abort(
                    grpc.StatusCode.UNAVAILABLE,
                    "Closing the stream because server is restarted. "
                    "This is expected and client is advised to reconnect.",
                )

    def _decode_rows(self, request, serialized_rows):
        if not serialized_rows:
            return []
        proto_rows = storage.AppendRowsRequest.pb(request).proto_rows
        if proto_rows.HasField("writer_schema"):
            descriptor = proto_rows.writer_schema.proto_descriptor
            key = descriptor.SerializeToString()
            if key != self._decoder_key:
                self._decoder = _RowDecoder(descriptor)
                self._decoder_key = key
        if self._decoder is None:
            return []
        return [self._decoder.decode(row) for row in serialized_rows]

    def append_rows_started(self, table):
        with self._lock:
            self._stream_counts[table] = self._stream_counts.get(table, 0) + 1

    def finalize_write_stream(self, request, context):
        table = _table_of(request.name)
        with self._lock:
            self._finalize_counts[table] = self._finalize_counts.get(table, 0) + 1
            row_count = self._row_counts.get(table, 0)
        return storage.FinalizeWriteStreamResponse(row_count=row_count)

    def request_restart_stream_after_next_batch(self):
        with self._lock:
            self._restart_stream_once = True

    def set_error(self, code, message):
        with self._lock:
            self._error = (code, message)

    def clear_error(self):
        with self._lock:
            self._error = None

    def get_rows(self, table):
        with self._lock:
            return list(self._rows.get(table, []))

    def get_row_count(self, table):
        with self._lock:
            return self._row_counts.get(table, 0)

    def get_batch_count(self, table):
        with self._lock:
            return self._batch_counts.get(table, 0)

    def get_stream_count(self, table):
        with self._lock:
            return self._stream_counts.get(table, 0)

    def get_create_count(self, table):
        with self._lock:
            return self._create_counts.get(table, 0)

    def get_finalize_count(self, table):
        with self._lock:
            return self._finalize_counts.get(table, 0)


def _make_handlers(servicer):
    def append_rows(request_iterator, context):
        counted = {"done": False}

        def counting_iterator():
            for request in request_iterator:
                if not counted["done"] and request.write_stream:
                    servicer.append_rows_started(_table_of(request.write_stream))
                    counted["done"] = True
                yield request

        return servicer.append_rows(counting_iterator(), context)

    return {
        "CreateWriteStream": grpc.unary_unary_rpc_method_handler(
            servicer.create_write_stream,
            request_deserializer=storage.CreateWriteStreamRequest.deserialize,
            response_serializer=stream.WriteStream.serialize,
        ),
        "AppendRows": grpc.stream_stream_rpc_method_handler(
            append_rows,
            request_deserializer=storage.AppendRowsRequest.deserialize,
            response_serializer=storage.AppendRowsResponse.serialize,
        ),
        "FinalizeWriteStream": grpc.unary_unary_rpc_method_handler(
            servicer.finalize_write_stream,
            request_deserializer=storage.FinalizeWriteStreamRequest.deserialize,
            response_serializer=storage.FinalizeWriteStreamResponse.serialize,
        ),
    }


class BigQueryIO():
    def __init__(self, port: int, host: str = "localhost") -> None:
        self.__port = port
        self.__host = host
        self.__server = None
        self.__servicer = None

    def start_listener(self) -> None:
        self.__servicer = _BigQueryWriteServicer()
        self.__server = grpc.server(futures.ThreadPoolExecutor(max_workers=8))
        self.__server.add_generic_rpc_handlers(
            (grpc.method_handlers_generic_handler(_SERVICE, _make_handlers(self.__servicer)),),
        )
        self.__server.add_insecure_port(f"{self.__host}:{self.__port}")
        self.__server.start()
        self.__wait_for_server_start()
        logger.info("BigQuery mock server started on port %d", self.__port)

    def stop_listener(self) -> None:
        if self.__server is None:
            return
        self.__server.stop(grace=0)
        self.__server = None
        self.__servicer = None
        logger.info("BigQuery mock server stopped")

    def restart_stream_after_next_batch(self) -> None:
        if self.__servicer is not None:
            self.__servicer.request_restart_stream_after_next_batch()

    def respond_with_error(self, code, message) -> None:
        if self.__servicer is not None:
            self.__servicer.set_error(code, message)

    def stop_responding_with_error(self) -> None:
        if self.__servicer is not None:
            self.__servicer.clear_error()

    def __wait_for_server_start(self):
        channel = grpc.insecure_channel(f"{self.__host}:{self.__port}")
        grpc.channel_ready_future(channel).result(timeout=10)
        channel.close()

    def read_logs(self, table: str) -> list:
        return self.__servicer.get_rows(table) if self.__servicer else []

    def read_log(self, table: str) -> dict:
        return self.read_logs(table)[0]

    def get_row_count(self, table: str) -> int:
        return self.__servicer.get_row_count(table) if self.__servicer else 0

    def get_batch_count(self, table: str) -> int:
        return self.__servicer.get_batch_count(table) if self.__servicer else 0

    def get_stream_count(self, table: str) -> int:
        return self.__servicer.get_stream_count(table) if self.__servicer else 0

    def get_create_count(self, table: str) -> int:
        return self.__servicer.get_create_count(table) if self.__servicer else 0

    def get_finalize_count(self, table: str) -> int:
        return self.__servicer.get_finalize_count(table) if self.__servicer else 0

#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Andras Mitzki <andras.mitzki@axoflow.com>
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
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import clickhouse_connect
import psutil
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.common.blocking import wait_until_true_custom
from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.executors.process_executor import ProcessExecutor
from axosyslog_light.syslog_ng_config.__init__ import destringify
from axosyslog_light.syslog_ng_config.__init__ import stringify
from tenacity import retry
from tenacity import stop_after_delay
from tenacity import wait_fixed

logger = logging.getLogger(__name__)

CLICKHOUSE_OPTIONS = {
    "database": "default",
    "table": "test_table",
    "user": "default",
    "password": f'{stringify("password")}',
}

CLICKHOUSE_OPTIONS_WITH_SCHEMA = {
    **CLICKHOUSE_OPTIONS,
    "schema": '"message" "String" => "$MSG"',
}


@dataclass
class ClickhouseServerTLS:
    cert_file: Path
    key_file: Path
    ca_cert_file: Optional[Path] = None
    require_client_auth: bool = False


class ClickhouseServerExecutor():
    def __init__(self, testcase_parameters) -> None:
        self.process = None
        self.clickhouse_server_ports = []
        self.hostname = "localhost"
        copy_shared_file(testcase_parameters, "clickhouse_server_config.xml")
        copy_shared_file(testcase_parameters, "clickhouse_users.xml")

    @staticmethod
    def _grpc_ssl_substitutions(tls: Optional[ClickhouseServerTLS]) -> dict:
        if tls is None:
            return {
                "ALLOCATED_GRPC_ENABLE_SSL": "false",
                "ALLOCATED_GRPC_SSL_CERT_FILE": "/path/to/ssl_cert_file",
                "ALLOCATED_GRPC_SSL_KEY_FILE": "/path/to/ssl_key_file",
                "ALLOCATED_GRPC_SSL_REQUIRE_CLIENT_AUTH": "false",
                "ALLOCATED_GRPC_SSL_CA_CERT_FILE": "/path/to/ssl_ca_cert_file",
            }
        return {
            "ALLOCATED_GRPC_ENABLE_SSL": "true",
            "ALLOCATED_GRPC_SSL_CERT_FILE": str(Path(tls.cert_file).resolve()),
            "ALLOCATED_GRPC_SSL_KEY_FILE": str(Path(tls.key_file).resolve()),
            "ALLOCATED_GRPC_SSL_REQUIRE_CLIENT_AUTH": "true" if tls.require_client_auth else "false",
            "ALLOCATED_GRPC_SSL_CA_CERT_FILE": str(Path(tls.ca_cert_file).resolve()) if tls.ca_cert_file else "/path/to/ssl_ca_cert_file",
        }

    def start(self, clickhouse_ports, tls: Optional[ClickhouseServerTLS] = None) -> None:
        command = [
            "clickhouse-server", "start",
            "--config-file", "clickhouse_server_config.xml",
        ]

        self.clickhouse_server_ports.append(clickhouse_ports.http_port)
        self.clickhouse_server_ports.append(clickhouse_ports.grpc_port)

        substitutions = {
            "ALLOCATED_HTTP_PORT": str(clickhouse_ports.http_port),
            "ALLOCATED_INTERSERVER_GRPC_PORT": str(clickhouse_ports.grpc_port),
            **self._grpc_ssl_substitutions(tls),
        }
        with open("clickhouse_server_config.xml", "r", encoding="utf-8") as file:
            config = file.read()
        for placeholder, value in substitutions.items():
            config = config.replace(placeholder, value)
        with open("clickhouse_server_config.xml", "w", encoding="utf-8") as file:
            file.write(config)

        self.process = ProcessExecutor().start(command, "clickhouse_server.stdout", "clickhouse_server.stderr")
        if wait_until_true_custom(lambda: psutil.Process(self.process.pid).children(), timeout=0.5):
            logger.info("Clickhouse server started with child process.")
            self.process = psutil.Process(self.process.pid).children()[0]
        self.wait_for_server_start(port=clickhouse_ports.http_port)
        logger.info("Clickhouse server started with PID: %s", self.process.pid)

    def stop(self) -> None:
        self.process.terminate()
        self.wait_for_server_stop()
        logger.info("Clickhouse server with PID %s terminated.", self.process.pid)

    @retry(wait=wait_fixed(0.1), reraise=True, stop=stop_after_delay(10))
    def wait_for_server_start(self, port) -> None:
        client = clickhouse_connect.get_client(
            host=self.hostname,
            username=CLICKHOUSE_OPTIONS["user"],
            password=destringify(CLICKHOUSE_OPTIONS["password"]),
            port=port,
        )
        try:
            client.command("SHOW DATABASES;")
        except Exception as e:
            raise RuntimeError("Clickhouse server is not ready yet.") from e

    def wait_for_server_stop(self) -> None:
        wait_until_true(lambda: not self.process.is_running())

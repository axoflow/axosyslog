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
import json
import logging

import clickhouse_connect

logger = logging.getLogger(__name__)


class ClickhouseClient():
    def __init__(self, username: str, password: str, http_port: int) -> None:
        self.username = username
        self.password = password
        self.http_port = http_port
        self.table_name = None

    def create_client(self):
        return clickhouse_connect.get_client(host="localhost", username=self.username, password=self.password, port=self.http_port)

    def create_table(self, table_name: str, table_columns_and_types: list[tuple[str, str]] = None) -> None:
        def construct_create_table_query(table_name: str, table_columns_and_types: list[tuple[str, str]]) -> str:
            self.table_name = table_name
            columns_definition = ", ".join([f'"{col}" {typ}' for col, typ in table_columns_and_types])
            prim_key_col_name = table_columns_and_types[0][0]
            create_table_query = f"CREATE TABLE {self.table_name} ({columns_definition}) ENGINE MergeTree() PRIMARY KEY (%s);" % prim_key_col_name
            return create_table_query

        create_table_query = construct_create_table_query(table_name, table_columns_and_types)

        client = self.create_client()
        client.command(create_table_query)
        client.close()

        logger.info(f"Table created: {table_name} with {create_table_query}")

    def delete_table(self) -> None:
        if self.table_name:
            client = self.create_client()
            client.command(f'DROP TABLE IF EXISTS {self.table_name};')
            client.close()
            logger.info(f"Table deleted: {self.table_name}")

    def select_all_from_table(self, table_name: str) -> list:
        if not table_name:
            raise ValueError("Table name must be provided to select data.")
        client = self.create_client()
        query = f"SELECT * FROM {table_name} FORMAT JSONEachRow;"
        logger.info(f"Executing Clickhouse query: {query}")
        try:
            query_res = client.command(query)
            rows = [json.loads(line) for line in query_res.splitlines()]
            logger.info(f"Query returned {len(rows)} rows")
            return rows
        except Exception as e:
            logger.error(f"Error executing query: {e}")
            return []
        finally:
            client.close()

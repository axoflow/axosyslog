#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import typing

import requests


class HttpIO():
    def __init__(self, port: typing.Optional[int] = None, use_tls: bool = False) -> None:
        if port is None:
            port = 443 if use_tls else 80
        self.__url = f"{'https' if use_tls else 'http'}://localhost:{port}/"

    def write_message(self, message: str) -> None:
        response = requests.post(self.__url, data=message)
        if response.status_code != 200:
            raise Exception(f"Failed to send message: {response.status_code} {response.text}")

    def write_json_message(self, message: dict) -> None:
        response = requests.post(self.__url, json=message)
        if response.status_code != 200:
            raise Exception(f"Failed to send message: {response.status_code} {response.text}")

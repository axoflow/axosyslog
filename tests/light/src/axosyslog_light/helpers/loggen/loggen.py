#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 One Identity
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
from dataclasses import dataclass
from pathlib import Path

import axosyslog_light.testcase_parameters.testcase_parameters as tc_parameters
from axosyslog_light.executors.process_executor import ProcessExecutor
from psutil import Popen
from psutil import TimeoutExpired


@dataclass
class LoggenStartParams:
    target: str
    port: int
    inet: bool = False
    unix: bool = False
    stream: bool = False
    dgram: bool = False
    use_ssl: bool = False
    dont_parse: bool = False
    read_file: typing.Optional[Path] = None
    skip_tokens: typing.Optional[str] = None
    loop_reading: bool = False
    rate: typing.Optional[int] = None
    interval: typing.Optional[int] = None
    permanent: bool = None
    syslog_proto: bool = None
    proxied: typing.Optional[int] = None
    sdata: bool = False
    no_framing: bool = False
    active_connections: typing.Optional[int] = None
    idle_connections: typing.Optional[int] = None
    ipv6: bool = False
    debug: bool = False
    number: typing.Optional[int] = None
    csv: bool = False
    quiet: bool = False
    size: typing.Optional[int] = None
    reconnect: bool = False
    proxied_tls_passthrough: bool = False
    proxy_src_ip: typing.Optional[str] = None
    proxy_dst_ip: typing.Optional[str] = None
    proxy_src_port: typing.Optional[int] = None
    proxy_dst_port: typing.Optional[int] = None

    def format(self) -> typing.List[str]:
        params = []
        if self.inet is True:
            params.append("--inet")
        if self.unix is True:
            params.append("--unix")
        if self.stream is True:
            params.append("--stream")
        if self.dgram is True:
            params.append("--dgram")
        if self.use_ssl is True:
            params.append("--use-ssl")
        if self.dont_parse is True:
            params.append("--dont-parse")
        if self.read_file is not None:
            params.append(f"--read-file={self.read_file}")
        if self.skip_tokens is not None:
            params.append(f"--skip-tokens={self.skip_tokens}")
        if self.loop_reading is True:
            params.append("--loop-reading")
        if self.rate is not None:
            params.append(f"--rate={self.rate}")
        if self.interval is not None:
            params.append(f"--interval={self.interval}")
        if self.permanent is True:
            params.append("--permanent")
        if self.syslog_proto is True:
            params.append("--syslog-proto")
        if self.proxied is True:
            params.append("--proxied")
        if self.proxied is not None:
            if self.proxied not in {1, 2}:
                raise ValueError(f"Proxied version must be 1 or 2, got {self.proxied}")
            params.append(f"--proxied={self.proxied}")
        if self.proxy_src_ip is not None:
            params.append(f"--proxy-src-ip={self.proxy_src_ip}")
        if self.proxy_dst_ip is not None:
            params.append(f"--proxy-dst-ip={self.proxy_dst_ip}")
        if self.proxy_src_port is not None:
            params.append(f"--proxy-src-port={self.proxy_src_port}")
        if self.proxy_dst_port is not None:
            params.append(f"--proxy-dst-port={self.proxy_dst_port}")
        if self.proxied_tls_passthrough is True:
            params.append("--proxied-tls-passthrough")
        if self.sdata is True:
            params.append("--sdata")
        if self.no_framing is True:
            params.append("--no-framing")
        if self.active_connections is not None:
            params.append(f"--active-connections={self.active_connections}")
        if self.idle_connections is not None:
            params.append(f"--idle-connections={self.idle_connections}")
        if self.ipv6 is True:
            params.append("--ipv6")
        if self.debug is True:
            params.append("--debug")
        if self.number is not None:
            params.append(f"--number={self.number}")
        if self.csv is True:
            params.append("--csv")
        if self.quiet is True:
            params.append("--quiet")
        if self.size is not None:
            params.append(f"--size={self.size}")
        if self.reconnect is True:
            params.append("--reconnect")
        params.append(self.target)
        params.append(str(self.port))
        return params


class Loggen(object):

    instanceIndex = -1

    @staticmethod
    def __get_new_instance_index():
        Loggen.instanceIndex += 1
        return Loggen.instanceIndex

    def __init__(self):
        self.loggen_proc = None
        self.loggen_bin_path = tc_parameters.INSTANCE_PATH.get_loggen_bin()

    def start(self, start_params: LoggenStartParams) -> Popen:
        if self.loggen_proc is not None and self.loggen_proc.is_running():
            raise Exception("Loggen is already running, you shouldn't call start")

        instanceIndex = Loggen.__get_new_instance_index()
        self.loggen_stdout_path = Path("loggen_stdout_{}".format(instanceIndex))
        self.loggen_stderr_path = Path("loggen_stderr_{}".format(instanceIndex))
        self.loggen_proc = ProcessExecutor().start(
            [self.loggen_bin_path] + start_params.format(),
            self.loggen_stdout_path,
            self.loggen_stderr_path,
        )

        return self.loggen_proc

    def stop(self):
        if self.loggen_proc is None:
            return

        self.loggen_proc.terminate()
        try:
            self.loggen_proc.wait(4)
        except TimeoutExpired:
            self.loggen_proc.kill()

        self.loggen_proc = None

    def get_sent_message_count(self):
        if not self.loggen_stderr_path.exists():
            return 0

        # loggen puts the count= messages to the stderr
        f = open(self.loggen_stderr_path, "r")
        content = f.read()
        f.close()

        start_pattern = "count="
        if start_pattern not in content:
            return 0
        index_start = content.rindex(start_pattern) + len(start_pattern)

        end_pattern = ", "
        if end_pattern not in content:
            return 0
        index_end = content.find(end_pattern, index_start)

        return int(content[index_start:index_end])

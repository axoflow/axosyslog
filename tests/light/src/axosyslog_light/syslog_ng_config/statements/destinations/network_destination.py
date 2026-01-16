#!/usr/bin/env python
#############################################################################
# Copyright (c) 2022 One Identity
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
from axosyslog_light.driver_io.network.network_io import NetworkIO
from axosyslog_light.syslog_ng_config.statements.destinations.destination_driver import DestinationDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


def map_transport(transport):
    mapping = {
        "tcp": NetworkIO.Transport.TCP,
        "udp": NetworkIO.Transport.UDP,
        "tls": NetworkIO.Transport.TLS,
    }
    transport = transport.replace("_", "-").replace("'", "").replace('"', "").lower()

    return mapping[transport]


def create_io(ip, options):
    transport = options["transport"] if "transport" in options else "tcp"

    return NetworkIO(ip, options["port"], map_transport(transport), False)


class NetworkDestination(DestinationDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        ip: str,
        **options,
    ) -> None:
        self.driver_name = "network"
        self.ip = ip
        self.options = options
        self._io = None
        super(NetworkDestination, self).__init__(stats_handler, prometheus_stats_handler, [self.ip], options)

    def start_listener(self):
        if self._io is None:
            self._io = create_io(self.ip, self.options)
        self._io.start_listener()

    def stop_listener(self):
        if self._io is not None:
            self._io.stop_listener()

    def read_log(self):
        return self.read_logs(1)[0]

    def read_logs(self, counter):
        return self._io.read_number_of_messages(counter)

    def read_until_logs(self, logs):
        return self._io.read_until_messages(logs)

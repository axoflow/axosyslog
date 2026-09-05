#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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
from typing import List

from axosyslog_light.driver_io.s3.s3_io import S3Client
from axosyslog_light.syslog_ng_config.__init__ import stringify
from axosyslog_light.syslog_ng_config.statements.destinations.destination_driver import DestinationDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler

# Options whose value is a quoted string or a template in the s3() config.  Everything else
# (chunk_size(), upload_threads(), compression(), ...) is rendered verbatim as a bareword.
_STRING_OPTIONS = {
    "url",
    "bucket",
    "access_key",
    "secret_key",
    "role",
    "object_key",
    "object_key_timestamp",
    "object_key_suffix",
    "template",
    "region",
    "server_side_encryption",
    "kms_key",
    "storage_class",
    "canned_acl",
}


class S3Destination(DestinationDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        s3_client: S3Client,
        **options,
    ) -> None:
        self.driver_name = "s3"
        self.s3_client = s3_client
        self.bucket = options["bucket"]
        self.compression = bool(options.get("compression", False))
        rendered_options = {key: self.__render_value(key, value) for key, value in options.items()}
        self.options = rendered_options
        super(S3Destination, self).__init__(stats_handler, prometheus_stats_handler, None, rendered_options)

    @staticmethod
    def __render_value(key, value):
        if isinstance(value, bool):
            return "yes" if value else "no"
        if isinstance(value, str) and key in _STRING_OPTIONS:
            return stringify(value)
        return value

    def get_stats(self):
        # The s3() SCL block is implemented as a python() destination, so its metrics live under the
        # "python" driver rather than "s3".
        return self.stats_handler.get_stats(self.group_type, "python", self.driver_instance)

    def list_object_keys(self) -> List[str]:
        return self.s3_client.list_object_keys(self.bucket)

    def read_object_lines(self, key: str) -> List[str]:
        return self.s3_client.read_object_lines(self.bucket, key, compressed=self.compression)

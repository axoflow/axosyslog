#############################################################################
# Copyright (c) 2024 Attila Szakacs
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

import requests
from pathlib import Path

from .cdn import CDN


class CloudflareCDN(CDN):
    """
    A `CDN` implementation that can connect to a Cloudflare CDN instance.

    Example config:

    ```yaml
    vendor: "cloudflare"
    all:
      zone-id: "secret1"
      api-token: "secret2"
    ```
    """

    def __init__(self, zone_id: str, api_token: str) -> None:
        self.__zone_id = zone_id
        self.__api_token = api_token

        super().__init__()

    @staticmethod
    def get_config_keyword() -> str:
        return "cloudflare"

    @staticmethod
    def from_config(cfg: dict) -> CDN:
        return CloudflareCDN(
            zone_id=cfg["zone-id"],
            api_token=cfg["api-token"],
        )

    def _refresh_cache(self, path: Path) -> None:
        url = f"https://api.cloudflare.com/client/v4/zones/{self.__zone_id}/purge_cache"
        headers = {"Authorization": f"Bearer {self.__api_token}"}
        data = {"purge_everything": True}

        response = requests.post(url, headers=headers, json=data).json()
        if not response.get("success", False):
            raise Exception("Failed to refresh CDN cache. response: {}".format(response))

#############################################################################
# Copyright (c) 2023-2024 Axoflow
# Copyright (c) 2023-2024 László Várady
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

FROM alpine:3.21 AS apkbuilder

RUN apk add --update-cache \
      alpine-conf \
      alpine-sdk \
      sudo \
      git \
    && apk upgrade -a \
    && adduser -D builder \
    && addgroup builder abuild \
    && echo 'builder ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER builder
WORKDIR /home/builder

ADD --chown=builder:builder apkbuild .

RUN \
    cd axoflow/axosyslog \
    && abuild deps


RUN mkdir packages || true \
    && abuild-keygen -n -a -i \
    && git clone --depth 1 --branch v3.21.3 git://git.alpinelinux.org/aports

RUN \
    cd aports/main/json-c && \
    sed -i -e '/^pkgrel/s,$,0,' -e '/\$CMAKE_CROSSOPTS/s,$, -DENABLE_THREADING=ON,' APKBUILD && \
    CFLAGS=-fno-omit-frame-pointer abuild -r

RUN \
    cd aports/main/musl && \
    sed -i -e '/^pkgrel/s,$,0,' APKBUILD && \
    CFLAGS=-fno-omit-frame-pointer abuild -r

RUN \
    cd aports/community/grpc && \
    sed -i -e '/^pkgrel/s,$,0,' APKBUILD  && \
    CFLAGS=-fno-omit-frame-pointer abuild -r

CMD ["/bin/sh"]

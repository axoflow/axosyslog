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

ARG ALPINE_VERSION=3.21

# json-c is required (as we enable threading mode, which is not enabled by
# the Alpine package)
# the rest will just improve perf output (due to -fno-omit-frame-pointer
# compilation)
ARG REBUILD_DEPS="main/musl main/jemalloc main/json-c main/glib community/grpc main/python3"

FROM alpine:$ALPINE_VERSION
ARG ALPINE_VERSION
ARG REBUILD_DEPS

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


RUN mkdir packages || true \
    && USER=builder abuild-keygen -n -a -i \
    && git clone --depth 1 --branch "$ALPINE_VERSION-stable" git://git.alpinelinux.org/aports


ADD --chown=builder:builder apkbuild .

ENV CFLAGS=-fno-omit-frame-pointer
ENV CXXFLAGS=-fno-omit-frame-pointer
ENV REBUILD_DEPS=$REBUILD_DEPS

# ENABLE_THREADING needs to be enabled for json-c as we will be crashing
# otherwise
RUN \
    sed -i -e '/\$CMAKE_CROSSOPTS/s,$, -DENABLE_THREADING=ON,' aports/main/json-c/APKBUILD && \
    echo $REBUILD_DEPS && \
    for pkg in $REBUILD_DEPS; do \
	(cd aports/$pkg && \
        sed -i -e '/^pkgrel/s,[0-9]*$,999,' APKBUILD && \
        abuild -r); \
    done

RUN \
    cd axoflow/axosyslog \
    && abuild deps

CMD ["/bin/sh"]

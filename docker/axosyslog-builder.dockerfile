#############################################################################
# Copyright (c) 2023-2025 Axoflow
# Copyright (c) 2023-2025 László Várady
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

ARG ALPINE_VERSION=3.22

# improve perf output (due to -fno-omit-frame-pointer compilation)
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

ADD --chown=builder:builder apkbuild .

ENV CFLAGS=-fno-omit-frame-pointer CXXFLAGS=-fno-omit-frame-pointer REBUILD_DEPS=$REBUILD_DEPS

RUN \
    mkdir packages || true \
    && USER=builder abuild-keygen -n -a -i \
    && git clone --depth 1 --branch "$ALPINE_VERSION-stable" git://git.alpinelinux.org/aports \
    && echo $REBUILD_DEPS \
    && for pkg in $REBUILD_DEPS; do \
	(cd aports/$pkg && \
        sed -i -e '/^pkgrel/s,[0-9]*$,999,' APKBUILD && \
        abuild -r); \
    done \
    && rm -rf aports \
    && cd axoflow/axosyslog \
    && abuild deps

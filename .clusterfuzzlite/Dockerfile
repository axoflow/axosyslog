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
# ClusterFuzzLite / OSS-Fuzz build image for the AxoSyslog filterx fuzz harnesses.
FROM gcr.io/oss-fuzz-base/base-builder

# The heavy optional modules (grpc, java, kafka, arrow-flight, ...) stay uninstalled, so
# configure skips them.
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        autoconf automake libtool \
        flex gperf pkg-config \
        ca-certificates curl xz-utils \
        libglib2.0-dev libssl-dev libpcre2-dev libjson-c-dev libnet1-dev zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*

# The base-builder image is Ubuntu 20.04, whose build tools are too old for AxoSyslog:
# its autoconf-archive (2019) predates AX_CXX_COMPILE_STDCXX(20) (configure.ac requires
# C++20, so aclocal fails during autogen.sh), and its bison (3.5.1) is older than the
# 3.7.6 the grammar needs.
RUN curl -fsSL https://ftp.gnu.org/gnu/autoconf-archive/autoconf-archive-2023.02.20.tar.xz \
        | tar xJ -C /tmp && \
    cp /tmp/autoconf-archive-2023.02.20/m4/*.m4 /usr/share/aclocal/ && \
    rm -rf /tmp/autoconf-archive-2023.02.20 && \
    curl -fsSL https://ftp.gnu.org/gnu/bison/bison-3.8.2.tar.xz | tar xJ -C /tmp && \
    cd /tmp/bison-3.8.2 && ./configure --prefix=/usr/local --disable-nls && \
    make -j"$(nproc)" && make install && cd / && rm -rf /tmp/bison-3.8.2

COPY . $SRC/axosyslog
WORKDIR $SRC/axosyslog
# The OSS-Fuzz builder executes $SRC/build.sh, not the copy inside the checkout.
COPY .clusterfuzzlite/build.sh $SRC/

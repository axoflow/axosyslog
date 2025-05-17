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

ARG DEBUG=false

FROM alpine:3.21 as apkbuilder

ARG PKG_TYPE=stable
ARG SNAPSHOT_VERSION
ARG DEBUG

RUN apk add --update-cache \
      alpine-conf \
      alpine-sdk \
      sudo \
    && apk upgrade -a \
    && adduser -D builder \
    && addgroup builder abuild \
    && echo 'builder ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER builder
WORKDIR /home/builder
ADD --chown=builder:builder apkbuild .

RUN [ $DEBUG = false ] || patch -d axoflow/axosyslog -p1 -i APKBUILD-debug.patch

RUN mkdir packages \
    && abuild-keygen -n -a -i \
    && printf 'export JOBS=$(nproc)\nexport MAKEFLAGS=-j$JOBS\n' >> .abuild/abuild.conf \
    && cd axoflow/json-c && abuild -r && cd - \
    && cd axoflow/axosyslog \
    && if [ "$PKG_TYPE" = "snapshot" ]; then \
        tarball_filename="$(ls axosyslog-*.tar.*)"; \
        [ -z "$tarball_filename" ] && echo "Tarball for snapshot can not be found" && exit 1; \
        tarball_name="${tarball_filename/\.tar.*}"; \
        sed -i -e "s|^pkgver=.*|pkgver=$SNAPSHOT_VERSION|" -e "s|^builddir=.*|builddir=\"\$srcdir/$tarball_name\"|" APKBUILD; \
        sed -i -e "s|^source=.*|source=\"$tarball_filename|" APKBUILD; \
       fi \
    && abuild checksum \
    && abuild -r


FROM alpine:3.21

ARG DEBUG

# https://github.com/opencontainers/image-spec/blob/main/annotations.md
LABEL maintainer="axoflow.io"
LABEL org.opencontainers.image.title="AxoSyslog"
LABEL org.opencontainers.image.description="The scalable security data processor by Axoflow"
LABEL org.opencontainers.image.authors="Axoflow"
LABEL org.opencontainers.image.vendor="Axoflow"
LABEL org.opencontainers.image.licenses="GPL-3.0-only"
LABEL org.opencontainers.image.source="https://github.com/axoflow/axosyslog"
LABEL org.opencontainers.image.documentation="https://axoflow.com/docs/axosyslog/docs/"
LABEL org.opencontainers.image.url="https://axoflow.io/"

COPY --from=apkbuilder /home/builder/packages/ /tmp/
COPY --from=apkbuilder /home/builder/.abuild/*.pub /etc/apk/keys/

RUN apk add --repository /tmp/axoflow -U --upgrade --no-cache \
    jemalloc \
    libdbi-drivers \
    tzdata \
    json-c \
    axosyslog \
    axosyslog-add-contextual-data \
    axosyslog-amqp \
    axosyslog-cloud-auth \
    axosyslog-ebpf \
    axosyslog-examples \
    axosyslog-geoip2 \
    axosyslog-graphite \
    axosyslog-grpc \
    axosyslog-http \
    axosyslog-json \
    axosyslog-kafka \
    axosyslog-map-value-pairs \
    axosyslog-mongodb \
    axosyslog-mqtt \
    axosyslog-python3 \
    axosyslog-redis \
    axosyslog-riemann \
    axosyslog-scl \
    axosyslog-snmp \
    axosyslog-sql \
    axosyslog-stardate \
    axosyslog-stomp \
    axosyslog-tags-parser \
    axosyslog-xml && \
    rm -rf /tmp/axoflow

RUN [ $DEBUG = false ] || apk add -U --upgrade --no-cache \
    gdb \
    valgrind \
    strace \
    perf \
    vim \
    musl-dbg

EXPOSE 514/udp
EXPOSE 601/tcp
EXPOSE 6514/tcp

HEALTHCHECK --interval=2m --timeout=5s --start-period=30s CMD /usr/sbin/syslog-ng-ctl healthcheck --timeout 5
ENV LD_PRELOAD=/usr/lib/libjemalloc.so.2
ENTRYPOINT ["/usr/sbin/syslog-ng", "-F"]

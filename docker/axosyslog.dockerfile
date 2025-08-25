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

# must be in sync with axosyslog-builder
ARG ALPINE_VERSION=3.22

FROM ghcr.io/axoflow/axosyslog-builder:$ALPINE_VERSION AS apkbuilder

# NOTE:
# an alpine system with SDK and dev packages plus a few packages built under
# /home/builder/packages
# user builder, home directory /home/builder

ARG PKG_TYPE=stable
ARG SNAPSHOT_VERSION

ADD --chown=builder:builder apkbuild .

RUN mkdir packages || true \
    && cd axoflow/axosyslog \
    && if [ "$PKG_TYPE" = "snapshot" ]; then \
        tarball_filename="$(ls -tr axosyslog-*.tar.* | head -1)"; \
        [ -z "$tarball_filename" ] && echo "Tarball for snapshot can not be found" && exit 1; \
        tarball_name="${tarball_filename/\.tar.*}"; \
        sed -i -e "s|^pkgver=.*|pkgver=$SNAPSHOT_VERSION|" -e "s|^builddir=.*|builddir=\"\$srcdir/$tarball_name\"|" APKBUILD; \
        sed -i -e "s|^source=.*|source=\"$tarball_filename|" APKBUILD; \
       fi \
    && abuild checksum \
    && abuild -r


FROM alpine:$ALPINE_VERSION AS prod

RUN cp /etc/apk/repositories /etc/apk/repositories.bak

# copy packages from builder image (both the base image and the previous stage)
COPY --from=apkbuilder /home/builder/packages/ /tmp/packages
COPY --from=apkbuilder /home/builder/.abuild/*.pub /etc/apk/keys/

RUN ls -lR /tmp/packages

RUN apk add \
        --repository /tmp/packages/main \
        --repository /tmp/packages/community \
        --repository /tmp/packages/axoflow \
        -U --upgrade --no-cache \
    jemalloc \
    libdbi-drivers \
    tzdata \
    json-c \
    musl musl-dbg \
    gdb \
    strace \
    perf \
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
    rm -rf /tmp/packages


# flatten out prod image
FROM scratch
COPY --from=prod / /

# https://github.com/opencontainers/image-spec/blob/main/annotations.md
LABEL maintainer="axoflow.io" \
    org.opencontainers.image.title="AxoSyslog" \
    org.opencontainers.image.description="The scalable security data processor by Axoflow" \
    org.opencontainers.image.authors="Axoflow" \
    org.opencontainers.image.vendor="Axoflow" \
    org.opencontainers.image.licenses="GPL-3.0-only" \
    org.opencontainers.image.source="https://github.com/axoflow/axosyslog" \
    org.opencontainers.image.documentation="https://axoflow.com/docs/axosyslog/docs/" \
    org.opencontainers.image.url="https://axoflow.io/"

EXPOSE 514/tcp 514/udp 601/tcp 6514/tcp

HEALTHCHECK --interval=2m --timeout=5s --start-period=30s CMD /usr/sbin/syslog-ng-ctl healthcheck --timeout 5
ENV LD_PRELOAD=/usr/lib/libjemalloc.so.2
ENTRYPOINT ["/usr/sbin/syslog-ng", "-F"]

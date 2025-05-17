
FROM alpine:3.21
ENV OS_DISTRIBUTION=ubuntu
ENV OS_DISTRIBUTION_CODE_NAME=noble

ARG ARG_IMAGE_PLATFORM
ARG COMMIT
ENV IMAGE_PLATFORM=${ARG_IMAGE_PLATFORM}
LABEL COMMIT=${COMMIT}


#RUN /dbld/builddeps update_packages
#RUN /dbld/builddeps install_dbld_dependencies
#RUN /dbld/builddeps install_apt_packages
#RUN /dbld/builddeps install_debian_build_deps

WORKDIR /root
RUN apk add --update-cache \
      alpine-conf \
      alpine-sdk \
      sudo \
      bash \
      shadow \
    && apk upgrade -a
VOLUME /source
VOLUME /build
VOLUME /dbld

RUN apk add build-base bison bpftool clang curl-dev file flex geoip-dev glib-dev grpc-dev hiredis-dev json-c-dev libbpf-dev libdbi-dev libmaxminddb-dev libnet-dev librdkafka-dev libtool libunwind-dev libxml2-utils mongo-c-driver-dev net-snmp-dev openssl-dev>3 paho-mqtt-c-dev pcre-dev protobuf-dev protoc python3-dev rabbitmq-c-dev riemann-c-client-dev tzdata zlib-dev
RUN apk add git
RUN apk add perf
RUN apk add autoconf automake gperf

COPY images/entrypoint.sh /

ENTRYPOINT ["/entrypoint.sh"]

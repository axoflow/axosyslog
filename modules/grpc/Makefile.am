include modules/grpc/protos/Makefile.am

include modules/grpc/common/Makefile.am

include modules/grpc/otel/Makefile.am
include modules/grpc/loki/Makefile.am
include modules/grpc/bigquery/Makefile.am
include modules/grpc/pubsub/Makefile.am
include modules/grpc/clickhouse/Makefile.am

if ENABLE_GRPC
modules/grpc modules/grpc/ mod-grpc: mod-otel mod-loki mod-bigquery mod-clickhouse mod-pubsub
endif

.PHONY: modules/grpc/otel/ mod-otel modules/grpc/loki/ mod-loki modules/grpc/bigquery/ mod-bigquery modules/grpc/clickhouse/ mod-clickhouse modules/grpc/pubsub/ mod-pubsub

EXTRA_DIST += \
  modules/grpc/CMakeLists.txt

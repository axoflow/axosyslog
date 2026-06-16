if ENABLE_GRPC

if ! OS_TYPE_MACOS
modules_grpc_bigquery_tests_TESTS = \
  modules/grpc/bigquery/tests/test_bigquery_worker

check_PROGRAMS += ${modules_grpc_bigquery_tests_TESTS}
endif

modules_grpc_bigquery_tests_test_bigquery_worker_SOURCES = \
  modules/grpc/bigquery/tests/test-bigquery-worker.cpp

EXTRA_modules_grpc_bigquery_tests_test_bigquery_worker_DEPENDENCIES = \
  $(top_builddir)/modules/grpc/bigquery/libbigquery_cpp.la \
  $(top_builddir)/modules/grpc/protos/libgrpc-protos.la

modules_grpc_bigquery_tests_test_bigquery_worker_CXXFLAGS = \
  $(TEST_CXXFLAGS) \
  $(PROTOBUF_CFLAGS) $(GRPCPP_CFLAGS) \
  $(GRPC_COMMON_CFLAGS) \
  -I$(GOOGLEAPIS_PROTO_BUILDDIR) \
  -I$(top_srcdir)/modules/grpc \
  -I$(top_srcdir)/modules/grpc/bigquery \
  -I$(top_builddir)/modules/grpc/bigquery

modules_grpc_bigquery_tests_test_bigquery_worker_LDADD = \
  $(TEST_LDADD) \
  $(top_builddir)/modules/grpc/bigquery/libbigquery_cpp.la \
  $(top_builddir)/modules/grpc/protos/libgrpc-protos.la

endif

EXTRA_DIST += \
    modules/grpc/bigquery/tests/CMakeLists.txt

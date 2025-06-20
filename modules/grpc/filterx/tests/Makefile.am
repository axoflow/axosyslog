if ENABLE_GRPC

if ! OS_TYPE_MACOS
modules_grpc_filterx_tests_TESTS = \
  modules/grpc/filterx/tests/test_protobuf_message

check_PROGRAMS += ${modules_grpc_filterx_tests_TESTS}
endif

modules_grpc_filterx_tests_test_protobuf_message_CFLAGS = \
  $(TEST_CFLAGS) \
  $(GRPC_COMMON_CFLAGS) \
  -I$(top_srcdir)/modules/grpc \
  -I$(top_srcdir)/modules/grpc/filterx \
  -I$(top_builddir)/modules/grpc/filterx

modules_grpc_filterx_tests_test_protobuf_message_LDADD = \
  $(TEST_LDADD) \
  $(top_builddir)/modules/grpc/common/libgrpc-common.la \
  $(top_builddir)/modules/grpc/filterx/libgrpc-filterx.la


endif

EXTRA_DIST += \
    modules/grpc/filterx/tests/CMakeLists.txt

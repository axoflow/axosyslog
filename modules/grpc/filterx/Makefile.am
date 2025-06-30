if ENABLE_GRPC

noinst_LTLIBRARIES += modules/grpc/filterx/libgrpc-filterx-cpp.la

modules_grpc_filterx_libgrpc_filterx_cpp_la_SOURCES = \
  modules/grpc/filterx/func-protobuf-message.h \
  modules/grpc/filterx/func-protobuf-message.cpp

modules_grpc_filterx_libgrpc_filterx_cpp_la_CXXFLAGS = \
  $(AM_CXXFLAGS) \
  $(PROTOBUF_CFLAGS) \
  $(GRPCPP_CFLAGS) \
  $(GRPC_COMMON_CFLAGS) \
  -I$(top_srcdir)/modules/grpc/filterx \
  -I$(top_builddir)/modules/grpc/filterx \
  -I$(top_srcdir)/modules/grpc

modules_grpc_filterx_libgrpc_filterx_cpp_la_LIBADD = $(MODULE_DEPS_LIBS) $(PROTOBUF_LIBS) $(GRPCPP_LIBS)
modules_grpc_filterx_libgrpc_filterx_cpp_la_LDFLAGS = $(MODULE_LDFLAGS)
EXTRA_modules_grpc_filterx_libgrpc_filterx_cpp_la_DEPENDENCIES = $(MODULE_DEPS_LIBS)

module_LTLIBRARIES += modules/grpc/filterx/libgrpc-filterx.la

modules_grpc_filterx_libgrpc_filterx_la_SOURCES = \
  modules/grpc/filterx/grpc-filterx-plugin.c

modules_grpc_filterx_libgrpc_filterx_la_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  $(GRPC_COMMON_CFLAGS) \
  -I$(top_srcdir)/modules/grpc \
  -I$(top_srcdir)/modules/grpc/filterx \
  -I$(top_builddir)/modules/grpc/filterx

modules_grpc_filterx_libgrpc_filterx_la_LIBADD = \
  $(GRPC_COMMON_LIBS) \
  $(top_builddir)/modules/grpc/protos/libgrpc-protos.la \
  $(top_builddir)/modules/grpc/filterx/libgrpc-filterx-cpp.la

nodist_EXTRA_modules_grpc_filterx_libgrpc_filterx_la_SOURCES = force-cpp-linker-with-default-stdlib.cpp

modules_grpc_filterx_libgrpc_filterx_la_LDFLAGS = $(MODULE_LDFLAGS)

modules_grpc_filterx_libgrpc_filterx_la_DEPENDENCIES = \
  $(MODULE_DEPS_LIBS) \
  $(GRPC_COMMON_LIBS) \
  $(top_builddir)/modules/grpc/protos/libgrpc-protos.la \
  $(top_builddir)/modules/grpc/filterx/libgrpc-filterx-cpp.la

modules/grpc/filterx modules/grpc/filterx/ mod-grpc-filterx: modules/grpc/filterx/libgrpc-filterx.la
else
modules/grpc/filterx modules/grpc/filterx/ mod-grpc-filterx:
endif

EXTRA_DIST += \
  modules/grpc/filterx/CMakeLists.txt

.PHONY: modules/grpc/filterx/ mod-grpc-filterx

include modules/grpc/filterx/tests/Makefile.am

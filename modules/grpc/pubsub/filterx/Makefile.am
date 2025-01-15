if ENABLE_GRPC

# noinst: Built as part of a larger libpubsub module as static library
noinst_LTLIBRARIES += modules/grpc/pubsub/filterx/libfilterx.la

modules_grpc_pubsub_filterx_libfilterx_la_SOURCES = \
  modules/grpc/pubsub/filterx/object-pubsub.h \
  modules/grpc/pubsub/filterx/object-pubsub-message.cpp \
  modules/grpc/pubsub/filterx/object-pubsub-message.hpp

modules_grpc_pubsub_filterx_libfilterx_la_CXXFLAGS = \
  $(AM_CXXFLAGS) \
  $(PROTOBUF_CFLAGS) \
  $(GRPCPP_CFLAGS) \
  -I$(GOOGLEAPIS_PROTO_BUILDDIR) \
  -I$(top_srcdir)/modules/grpc/pubsub/filterx \
  -I$(top_builddir)/modules/grpc/pubsub/filterx \
  -Wno-deprecated-declarations

modules_grpc_pubsub_filterx_libfilterx_la_LIBADD = \
  $(MODULE_DEPS_LIBS) \
  $(PROTOBUF_LIBS) \
  $(top_builddir)/modules/grpc/protos/libgrpc-protos.la

modules_grpc_pubsub_filterx_libfilterx_la_LDFLAGS = $(MODULE_LDFLAGS)

EXTRA_modules_grpc_pubsub_filterx_libfilterx_la_DEPENDENCIES = \
  $(MODULE_DEPS_LIBS) \
  $(top_builddir)/modules/grpc/protos/libgrpc-protos.la

endif

EXTRA_DIST += \
  modules/grpc/pubsub/filterx/CMakeLists.txt

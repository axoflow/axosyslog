module_LTLIBRARIES				+= modules/lumberjack-proto/liblumberjack-proto.la
modules_lumberjack_proto_liblumberjack_proto_la_SOURCES	=	\
	modules/lumberjack-proto/lumberjack-proto-grammar.y	\
	modules/lumberjack-proto/lumberjack-proto-options.c	\
	modules/lumberjack-proto/lumberjack-proto-options.h	\
	modules/lumberjack-proto/lumberjack-proto-parser.c	\
	modules/lumberjack-proto/lumberjack-proto-parser.h	\
	modules/lumberjack-proto/lumberjack-proto-plugin.c	\
	modules/lumberjack-proto/logproto-lumberjack-server.c	\
	modules/lumberjack-proto/logproto-lumberjack-server.h

modules_lumberjack_proto_liblumberjack_proto_la_CPPFLAGS	=	\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)/modules/lumberjack-proto		\
	-I$(top_builddir)/modules/lumberjack-proto
modules_lumberjack_proto_liblumberjack_proto_la_LIBADD	=	\
	$(MODULE_DEPS_LIBS)
modules_lumberjack_proto_liblumberjack_proto_la_LDFLAGS	=	\
	$(MODULE_LDFLAGS)
modules_lumberjack_proto_liblumberjack_proto_la_CFLAGS	=	\
	$(AM_CFLAGS) $(MODULE_CFLAGS)
EXTRA_modules_lumberjack_proto_liblumberjack_proto_la_DEPENDENCIES	=	\
	$(MODULE_DEPS_LIBS)

BUILT_SOURCES						+=	\
	modules/lumberjack-proto/lumberjack-proto-grammar.y	\
	modules/lumberjack-proto/lumberjack-proto-grammar.c	\
	modules/lumberjack-proto/lumberjack-proto-grammar.h
EXTRA_DIST						+=	\
	modules/lumberjack-proto/lumberjack-proto-grammar.ym	\
	modules/lumberjack-proto/CMakeLists.txt

modules/lumberjack-proto modules/lumberjack-proto/ mod-lumberjack-proto: modules/lumberjack-proto/liblumberjack-proto.la
.PHONY: modules/lumberjack-proto/ mod-lumberjack-proto

include modules/lumberjack-proto/tests/Makefile.am

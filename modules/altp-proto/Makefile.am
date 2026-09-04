module_LTLIBRARIES				+= modules/altp-proto/libaltp-proto.la
modules_altp_proto_libaltp_proto_la_SOURCES	=	\
	modules/altp-proto/altp-proto-grammar.y		\
	modules/altp-proto/altp-proto-client-grammar.y	\
	modules/altp-proto/altp-session.c		\
	modules/altp-proto/altp-session.h		\
	modules/altp-proto/altp-proto-options.c		\
	modules/altp-proto/altp-proto-options.h		\
	modules/altp-proto/altp-proto-parser.c		\
	modules/altp-proto/altp-proto-parser.h		\
	modules/altp-proto/altp-proto-client-parser.c	\
	modules/altp-proto/altp-proto-client-parser.h	\
	modules/altp-proto/altp-proto-plugin.c		\
	modules/altp-proto/logproto-altp-client.c	\
	modules/altp-proto/logproto-altp-client.h	\
	modules/altp-proto/logproto-altp-server.c	\
	modules/altp-proto/logproto-altp-server.h

modules_altp_proto_libaltp_proto_la_CPPFLAGS	=	\
	$(AM_CPPFLAGS)					\
	-I$(top_srcdir)/modules/altp-proto		\
	-I$(top_builddir)/modules/altp-proto
modules_altp_proto_libaltp_proto_la_LIBADD	=	\
	$(MODULE_DEPS_LIBS) $(OPENSSL_LIBS)
modules_altp_proto_libaltp_proto_la_LDFLAGS	=	\
	$(MODULE_LDFLAGS)
modules_altp_proto_libaltp_proto_la_CFLAGS	=	\
	$(AM_CFLAGS) $(MODULE_CFLAGS) $(OPENSSL_CFLAGS)
EXTRA_modules_altp_proto_libaltp_proto_la_DEPENDENCIES	=	\
	$(MODULE_DEPS_LIBS)

BUILT_SOURCES					+=	\
	modules/altp-proto/altp-proto-grammar.y		\
	modules/altp-proto/altp-proto-grammar.c		\
	modules/altp-proto/altp-proto-grammar.h		\
	modules/altp-proto/altp-proto-client-grammar.y	\
	modules/altp-proto/altp-proto-client-grammar.c	\
	modules/altp-proto/altp-proto-client-grammar.h
EXTRA_DIST					+=	\
	modules/altp-proto/altp-proto-grammar.ym	\
	modules/altp-proto/altp-proto-client-grammar.ym	\
	modules/altp-proto/CMakeLists.txt

modules/altp-proto modules/altp-proto/ mod-altp-proto: modules/altp-proto/libaltp-proto.la
.PHONY: modules/altp-proto/ mod-altp-proto

include modules/altp-proto/tests/Makefile.am

modules_altp_proto_tests_TESTS			=	\
	modules/altp-proto/tests/test_altp_proto	\
	modules/altp-proto/tests/test_altp_client

check_PROGRAMS					+=	\
	${modules_altp_proto_tests_TESTS}

modules_altp_proto_tests_test_altp_proto_CFLAGS	=	\
	$(TEST_CFLAGS) -I$(top_srcdir)/modules/altp-proto
modules_altp_proto_tests_test_altp_proto_LDADD	=	\
	$(TEST_LDADD)
modules_altp_proto_tests_test_altp_proto_LDFLAGS	=	\
	$(PREOPEN_CORE)					\
	-dlpreopen $(top_builddir)/modules/altp-proto/libaltp-proto.la \
	-dlpreopen $(top_builddir)/modules/afsocket/libafsocket.la

modules_altp_proto_tests_test_altp_client_CFLAGS	=	\
	$(TEST_CFLAGS) -I$(top_srcdir)/modules/altp-proto
modules_altp_proto_tests_test_altp_client_LDADD	=	\
	$(TEST_LDADD)
modules_altp_proto_tests_test_altp_client_LDFLAGS	=	\
	$(PREOPEN_CORE)					\
	-dlpreopen $(top_builddir)/modules/altp-proto/libaltp-proto.la \
	-dlpreopen $(top_builddir)/modules/afsocket/libafsocket.la

EXTRA_DIST += modules/altp-proto/tests/CMakeLists.txt

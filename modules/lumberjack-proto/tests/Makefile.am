modules_lumberjack_proto_tests_TESTS			=	\
	modules/lumberjack-proto/tests/test_lumberjack_proto

check_PROGRAMS						+=	\
	${modules_lumberjack_proto_tests_TESTS}

modules_lumberjack_proto_tests_test_lumberjack_proto_CFLAGS	=	\
	$(TEST_CFLAGS) -I$(top_srcdir)/modules/lumberjack-proto
modules_lumberjack_proto_tests_test_lumberjack_proto_LDADD	=	\
	$(TEST_LDADD)
modules_lumberjack_proto_tests_test_lumberjack_proto_LDFLAGS	=	\
	$(PREOPEN_CORE)						\
	-dlpreopen $(top_builddir)/modules/lumberjack-proto/liblumberjack-proto.la \
	-dlpreopen $(top_builddir)/modules/afsocket/libafsocket.la

EXTRA_DIST += modules/lumberjack-proto/tests/CMakeLists.txt

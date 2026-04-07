if ENABLE_SQL

modules_afsql_tests_TESTS = \
	modules/afsql/tests/test_afsql_run_query

check_PROGRAMS += ${modules_afsql_tests_TESTS}

modules_afsql_tests_test_afsql_run_query_SOURCES = \
	modules/afsql/tests/mock-dbi.h \
	modules/afsql/tests/test_afsql.h \
	modules/afsql/tests/test_afsql_run_query.c \
	modules/afsql/afsql.c \
	modules/afsql/tests/mock-dbi.c

modules_afsql_tests_test_afsql_run_query_CFLAGS = \
	$(TEST_CFLAGS) \
	-I$(top_srcdir)/modules/afsql \
	-I$(top_srcdir)/modules/afsql/tests \
	$(LIBDBI_CFLAGS)

modules_afsql_tests_test_afsql_run_query_LDADD = \
	$(TEST_LDADD) \
	$(OPENSSL_LIBS)

modules_afsql_tests_test_afsql_run_query_LDFLAGS = \
	$(LDFLAGS)

endif

EXTRA_DIST += modules/afsql/tests/CMakeLists.txt

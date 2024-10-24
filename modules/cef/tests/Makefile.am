modules_cef_tests_TESTS		= \
	modules/cef/tests/test-format-cef-extension	\
	modules/cef/tests/test-filterx-function-parse-cef	\
	modules/cef/tests/test-filterx-function-parse-leef

check_PROGRAMS				+= ${modules_cef_tests_TESTS}

EXTRA_DIST += modules/cef/tests/CMakeLists.txt

modules_cef_tests_test_format_cef_extension_CFLAGS	= $(TEST_CFLAGS)
modules_cef_tests_test_format_cef_extension_LDADD	= $(TEST_LDADD)
modules_cef_tests_test_format_cef_extension_LDFLAGS	= \
	$(PREOPEN_SYSLOGFORMAT)		  \
	-dlpreopen $(top_builddir)/modules/cef/libcef.la

EXTRA_modules_cef_tests_test_format_cef_extension_DEPENDENCIES =	\
	$(top_builddir)/modules/cef/libcef.la

modules_cef_tests_test_filterx_function_parse_cef_SOURCES = \
	modules/cef/tests/test-filterx-function-parse-cef.c	\
	modules/cef/tests/test_helpers.c \
	modules/cef/tests/test_helpers.h

modules_cef_tests_test_filterx_function_parse_cef_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/cef
modules_cef_tests_test_filterx_function_parse_cef_LDADD		= $(TEST_LDADD)
modules_cef_tests_test_filterx_function_parse_cef_LDFLAGS	= \
	$(PREOPEN_SYSLOGFORMAT)		  \
	-dlpreopen $(top_builddir)/modules/cef/libcef.la

EXTRA_modules_cef_tests_test_filterx_function_parse_cef_DEPENDENCIES =	\
	$(top_builddir)/modules/cef/libcef.la

modules_cef_tests_test_filterx_function_parse_leef_SOURCES = \
	modules/cef/tests/test-filterx-function-parse-leef.c	\
	modules/cef/tests/test_helpers.c \
	modules/cef/tests/test_helpers.h

modules_cef_tests_test_filterx_function_parse_leef_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/cef
modules_cef_tests_test_filterx_function_parse_leef_LDADD	= $(TEST_LDADD)
modules_cef_tests_test_filterx_function_parse_leef_LDFLAGS	= \
	$(PREOPEN_SYSLOGFORMAT)		  \
	-dlpreopen $(top_builddir)/modules/cef/libcef.la

EXTRA_modules_cef_tests_test_filterx_function_parse_leef_DEPENDENCIES =	\
	$(top_builddir)/modules/cef/libcef.la

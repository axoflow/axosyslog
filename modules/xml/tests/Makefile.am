modules_xml_tests_TESTS		= \
	modules/xml/tests/test_xml_parser \
	modules/xml/tests/test_windows_eventlog_xml_parser \
	modules/xml/tests/test_filterx_parse_xml \
	modules/xml/tests/test_filterx_parse_windows_eventlog_xml

check_PROGRAMS				+= ${modules_xml_tests_TESTS}

modules_xml_tests_test_xml_parser_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/xml
modules_xml_tests_test_xml_parser_LDADD	= $(TEST_LDADD)
modules_xml_tests_test_xml_parser_LDFLAGS	= \
	$(PREOPEN_SYSLOGFORMAT)		  \
	-dlpreopen $(top_builddir)/modules/xml/libxml.la
EXTRA_modules_xml_tests_test_xml_parser_DEPENDENCIES = $(top_builddir)/modules/xml/libxml.la

modules_xml_tests_test_windows_eventlog_xml_parser_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/xml
modules_xml_tests_test_windows_eventlog_xml_parser_LDADD	= $(TEST_LDADD)
modules_xml_tests_test_windows_eventlog_xml_parser_LDFLAGS	= \
	$(PREOPEN_SYSLOGFORMAT)		  \
	-dlpreopen $(top_builddir)/modules/xml/libxml.la
EXTRA_modules_xml_tests_test_windows_eventlog_xml_parser_DEPENDENCIES = $(top_builddir)/modules/xml/libxml.la

modules_xml_tests_test_filterx_parse_xml_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/xml
modules_xml_tests_test_filterx_parse_xml_LDADD	= $(TEST_LDADD)
modules_xml_tests_test_filterx_parse_xml_LDFLAGS	= \
	$(PREOPEN_SYSLOGFORMAT)		  \
	-dlpreopen $(top_builddir)/modules/xml/libxml.la
EXTRA_modules_xml_tests_test_filterx_parse_xml_DEPENDENCIES = $(top_builddir)/modules/xml/libxml.la

modules_xml_tests_test_filterx_parse_windows_eventlog_xml_CFLAGS	= $(TEST_CFLAGS) -I$(top_srcdir)/modules/xml
modules_xml_tests_test_filterx_parse_windows_eventlog_xml_LDADD	= $(TEST_LDADD)
modules_xml_tests_test_filterx_parse_windows_eventlog_xml_LDFLAGS	= \
	$(PREOPEN_SYSLOGFORMAT)		  \
	-dlpreopen $(top_builddir)/modules/xml/libxml.la
EXTRA_modules_xml_tests_test_filterx_parse_windows_eventlog_xml_DEPENDENCIES = $(top_builddir)/modules/xml/libxml.la

EXTRA_DIST += modules/xml/tests/CMakeLists.txt

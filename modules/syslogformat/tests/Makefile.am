modules_syslogformat_tests_TESTS = \
    modules/syslogformat/tests/test_syslog_format \
    modules/syslogformat/tests/test_filterx_func_format_sdata

check_PROGRAMS += ${modules_syslogformat_tests_TESTS}

EXTRA_DIST += modules/syslogformat/tests/CMakeLists.txt

modules_syslogformat_tests_test_syslog_format_CFLAGS = $(TEST_CFLAGS) -I$(top_srcdir)/modules/syslogformat
modules_syslogformat_tests_test_syslog_format_LDADD = $(TEST_LDADD) $(PREOPEN_SYSLOGFORMAT)

modules_syslogformat_tests_test_filterx_func_format_sdata_CFLAGS = $(TEST_CFLAGS) -I$(top_srcdir)/modules/syslogformat
modules_syslogformat_tests_test_filterx_func_format_sdata_LDADD = $(TEST_LDADD) $(PREOPEN_SYSLOGFORMAT)

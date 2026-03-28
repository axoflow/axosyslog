if ENABLE_HTTP

modules_http_adapters_tests_TESTS                        = \
        modules/http-adapters/tests/test_splunk_adapter

check_PROGRAMS                                  += ${modules_http_adapters_tests_TESTS}

EXTRA_modules_http_adapters_tests_test_splunk_adapter_DEPENDENCIES =      \
        $(top_builddir)/modules/http-adapters/libhttp-adapters.la
modules_http_adapters_tests_test_splunk_adapter_CFLAGS     = $(TEST_CFLAGS) -I$(top_srcdir)/modules/http-adapters
modules_http_adapters_tests_test_splunk_adapter_LDADD      = $(TEST_LDADD)
modules_http_adapters_tests_test_splunk_adapter_LDFLAGS    = \
        -dlpreopen $(top_builddir)/modules/http-adapters/libhttp-adapters.la


endif

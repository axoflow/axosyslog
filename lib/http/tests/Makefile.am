lib_http_tests_TESTS = \
  lib/http/tests/test_http-message \
  lib/http/tests/test_http-parser

EXTRA_DIST += lib/http/tests/CMakeLists.txt

check_PROGRAMS += ${lib_http_tests_TESTS}

lib_http_tests_test_http_message_CFLAGS = $(TEST_CFLAGS)
lib_http_tests_test_http_message_LDADD = $(TEST_LDADD)

lib_http_tests_test_http_parser_CFLAGS = $(TEST_CFLAGS)
lib_http_tests_test_http_parser_LDADD = $(TEST_LDADD)

modules_splunk_s2s_tests_TESTS = \
  modules/splunk-s2s/tests/test_splunk_s2s_protocol

check_PROGRAMS += ${modules_splunk_s2s_tests_TESTS}

modules_splunk_s2s_tests_test_splunk_s2s_protocol_SOURCES = \
  modules/splunk-s2s/tests/test_splunk_s2s_protocol.c \
  modules/splunk-s2s/tests/test_splunk_s2s_protocol_vectors.h
modules_splunk_s2s_tests_test_splunk_s2s_protocol_CFLAGS = \
  $(TEST_CFLAGS) \
  -I$(top_srcdir)/modules/splunk-s2s
modules_splunk_s2s_tests_test_splunk_s2s_protocol_LDADD = \
  $(top_builddir)/modules/splunk-s2s/libsplunk_s2s_protocol.la \
  $(TEST_LDADD)

EXTRA_DIST += modules/splunk-s2s/tests/CMakeLists.txt

noinst_LTLIBRARIES += modules/splunk-s2s/libsplunk_s2s_protocol.la

modules_splunk_s2s_libsplunk_s2s_protocol_la_SOURCES = \
  modules/splunk-s2s/splunk-s2s-protocol.c \
  modules/splunk-s2s/splunk-s2s-protocol.h

modules_splunk_s2s_libsplunk_s2s_protocol_la_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  -I$(top_srcdir)/modules/splunk-s2s \
  -I$(top_builddir)/modules/splunk-s2s

modules_splunk_s2s_libsplunk_s2s_protocol_la_CFLAGS = $(AM_CFLAGS) $(MODULE_CFLAGS)
modules_splunk_s2s_libsplunk_s2s_protocol_la_LIBADD = $(MODULE_DEPS_LIBS)
modules_splunk_s2s_libsplunk_s2s_protocol_la_LDFLAGS = $(MODULE_LDFLAGS)
EXTRA_modules_splunk_s2s_libsplunk_s2s_protocol_la_DEPENDENCIES = $(MODULE_DEPS_LIBS)

noinst_LTLIBRARIES += modules/splunk-s2s/libsplunk_s2s_client.la

modules_splunk_s2s_libsplunk_s2s_client_la_SOURCES = \
  modules/splunk-s2s/splunk-s2s-proto-client.c \
  modules/splunk-s2s/splunk-s2s-proto-client.h

modules_splunk_s2s_libsplunk_s2s_client_la_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  -I$(top_srcdir)/modules/splunk-s2s \
  -I$(top_builddir)/modules/splunk-s2s

modules_splunk_s2s_libsplunk_s2s_client_la_CFLAGS = $(AM_CFLAGS) $(MODULE_CFLAGS)
modules_splunk_s2s_libsplunk_s2s_client_la_LIBADD = $(MODULE_DEPS_LIBS)
modules_splunk_s2s_libsplunk_s2s_client_la_LDFLAGS = $(MODULE_LDFLAGS)
EXTRA_modules_splunk_s2s_libsplunk_s2s_client_la_DEPENDENCIES = $(MODULE_DEPS_LIBS)

EXTRA_DIST += modules/splunk-s2s/CMakeLists.txt

include modules/splunk-s2s/tests/Makefile.am

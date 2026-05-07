if ENABLE_NATIVE

# we are building a static archive in PIC mode, so it can be linked into an .so
# that is produced by an external build process.

export top_srcdir
export top_builddir

lib_LTLIBRARIES += modules/native/libsyslog-ng-native-connector.la
modules_native_libsyslog_ng_native_connector_la_SOURCES = \
  modules/native/native-grammar.y       \
  modules/native/native-parser.c        \
  modules/native/native-parser.h        \
  modules/native/parser.c		\
  modules/native/parser.h

pkgconfig_DATA += autotools/syslog-ng-native-connector.pc

modules_native_libsyslog_ng_native_connector_la_CFLAGS = \
  $(AM_CFLAGS) \
  -fvisibility=hidden	\
  -static \
  -fPIC \
  -I$(top_srcdir)/modules/native        \
  -I$(top_builddir)/modules/native

else
modules/native modules/native/ mod-native: modules/native/libnative.la
endif

BUILT_SOURCES       +=      \
  modules/native/native-grammar.y       \
  modules/native/native-grammar.c       \
  modules/native/native-grammar.h

EXTRA_DIST        +=      \
  modules/native/native-grammar.ym	\
  modules/native/CMakeLists.txt

.PHONY: modules/native/ mod-native

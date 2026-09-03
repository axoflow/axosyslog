if ENABLE_HTTP

noinst_LTLIBRARIES += modules/http/source/libehttp-source.la

modules_http_source_libehttp_source_la_SOURCES = \
  modules/http/source/http-decompression.c \
  modules/http/source/http-decompression.h \
  modules/http/source/http-source-grammar.y \
  modules/http/source/http-source-parser.c \
  modules/http/source/http-source-parser.h \
  modules/http/source/http-source.h \
  modules/http/source/http-source.c

modules_http_source_libehttp_source_la_CPPFLAGS  = \
  $(AM_CPPFLAGS) \
  $(JSON_CFLAGS) \
  $(ZLIB_CFLAGS) \
  -I$(top_srcdir)/modules/http/source \
  -I$(top_builddir)/modules/http/source

modules_http_source_libehttp_source_la_LIBADD  = $(MODULE_DEPS_LIBS) $(JSON_LIBS) $(ZLIB_LIBS)
modules_http_source_libehttp_source_la_LDFLAGS = $(MODULE_LDFLAGS)
modules_http_source_libehttp_source_la_DEPENDENCIES = $(MODULE_DEPS_LIBS) $(JSON_DEPENDENCY)

endif

BUILT_SOURCES += \
  modules/http/source/http-source-grammar.y \
  modules/http/source/http-source-grammar.c \
  modules/http/source/http-source-grammar.h

EXTRA_DIST += \
  modules/http/source/http-source-grammar.ym \
  modules/http/source/CMakeLists.txt

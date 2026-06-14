if ENABLE_HTTP_SOURCE

module_LTLIBRARIES                  += modules/http-source/libhttp-source.la
modules_http_source_libhttp_source_la_SOURCES = \
  modules/http-source/http-source.h               \
  modules/http-source/http-source.c               \
  modules/http-source/http-server-listener.h      \
  modules/http-source/http-server-listener.c      \
  modules/http-source/http-source-config.h        \
  modules/http-source/http-source-config.c        \
  modules/http-source/http-source-grammar.y       \
  modules/http-source/http-source-parser.c        \
  modules/http-source/http-source-parser.h        \
  modules/http-source/http-source-plugin.c        \
  modules/http-source/llhttp/llhttp.h             \
  modules/http-source/llhttp/llhttp.c             \
  modules/http-source/llhttp/api.c                \
  modules/http-source/llhttp/http.c

modules_http_source_libhttp_source_la_CPPFLAGS  = \
  $(AM_CPPFLAGS)                          \
  -I$(top_srcdir)/modules/http-source     \
  -I$(top_builddir)/modules/http-source   \
  -I$(top_srcdir)/modules/http-source/llhttp

modules_http_source_libhttp_source_la_LIBADD  = $(MODULE_DEPS_LIBS)

modules_http_source_libhttp_source_la_LDFLAGS = $(MODULE_LDFLAGS)
modules_http_source_libhttp_source_la_CFLAGS = $(AM_CFLAGS) $(MODULE_CFLAGS)

EXTRA_modules_http_source_libhttp_source_la_DEPENDENCIES = $(MODULE_DEPS_LIBS)

modules/http-source modules/http-source/ mod-http-source: modules/http-source/libhttp-source.la
else
modules/http-source modules/http-source/ mod-http-source:
endif

BUILT_SOURCES       +=      \
  modules/http-source/http-source-grammar.y       \
  modules/http-source/http-source-grammar.c       \
  modules/http-source/http-source-grammar.h

EXTRA_DIST        +=      \
  modules/http-source/http-source-grammar.ym       \
  modules/http-source/llhttp/LICENSE               \
  modules/http-source/CMakeLists.txt

.PHONY: modules/http-source/ mod-http-source

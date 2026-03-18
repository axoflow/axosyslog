if ENABLE_HTTP

module_LTLIBRARIES      += modules/http-adapters/libhttp-adapters.la
modules_http_adapters_libhttp_adapters_la_SOURCES = \
  modules/http-adapters/http-adapter.c \
  modules/http-adapters/http-adapter.h \
  modules/http-adapters/splunk-adapter.c \
  modules/http-adapters/splunk-adapter.h \
  modules/http-adapters/http-adapters-grammar.y \
  modules/http-adapters/http-adapters-plugin.c \
  modules/http-adapters/http-adapters-parser.h \
  modules/http-adapters/http-adapters-parser.c

modules_http_adapters_libhttp_adapters_la_CPPFLAGS  = \
  $(AM_CPPFLAGS)                         \
  $(JSON_CFLAGS)                         \
  -I$(top_srcdir)/modules/http-adapters  \
  -I$(top_builddir)/modules/http-adapters

modules_http_adapters_libhttp_adapters_la_LIBADD  = $(MODULE_DEPS_LIBS) $(JSON_LIBS)
modules_http_adapters_libhttp_adapters_la_LDFLAGS = $(MODULE_LDFLAGS)

EXTRA_modules_http_adapters_libhttp_adapters_la_DEPENDENCIES= $(MODULE_DEPS_LIBS) $(JSON_DEPENDENCY)

modules/http-adapters modules/http-adapters/ mod-http-adapters: modules/http-adapters/libhttp-adapters.la
else
modules/http-adapters modules/http-adapters/ mod-http-adapters:
endif

BUILT_SOURCES       +=      \
  modules/http-adapters/http-adapters-grammar.y       \
  modules/http-adapters/http-adapters-grammar.c       \
  modules/http-adapters/http-adapters-grammar.h

EXTRA_DIST        +=      \
  modules/http-adapters/http-adapters-grammar.ym	\
  modules/http-adapters/CMakeLists.txt

.PHONY: modules/http-adapters/ mod-http-adapters

include modules/http-adapters/tests/Makefile.am

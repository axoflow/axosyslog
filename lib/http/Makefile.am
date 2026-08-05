httpincludedir = ${pkgincludedir}/http

EXTRA_DIST += lib/http/CMakeLists.txt

httpinclude_HEADERS = \
  lib/http/buffer.h \
  lib/http/http-status.h \
  lib/http/http-message.h \
  lib/http/http-message-internal.h \
  lib/http/http-parser.h \
  lib/http/logproto-http-server.h

http_sources = \
  lib/http/http-message.c \
  lib/http/http-parser.c \
  lib/http/logproto-http-server.c

noinst_LTLIBRARIES += lib/http/libllhttp.la

lib_http_libllhttp_la_SOURCES = \
  lib/http/llhttp/src/llhttp.c \
  lib/http/llhttp/src/api.c \
  lib/http/llhttp/src/http.c

EXTRA_DIST += \
  lib/http/llhttp/include/llhttp.h \
  lib/http/llhttp/LICENSE \
  lib/http/llhttp/LICENSE-MIT

lib_http_libllhttp_la_CFLAGS = \
  $(AM_CFLAGS) \
  -isystem $(top_srcdir)/lib/http/llhttp/include \
  -Wno-switch-default

include lib/http/tests/Makefile.am

httpincludedir = ${pkgincludedir}/http

EXTRA_DIST += lib/http/CMakeLists.txt

httpinclude_HEADERS = \
  lib/http/buffer.h \
  lib/http/http-status.h \
  lib/http/http-message.h \
  lib/http/http-message-internal.h

http_sources = \
  lib/http/http-message.c

include lib/http/tests/Makefile.am

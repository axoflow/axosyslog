if ENABLE_HTTP

noinst_LTLIBRARIES += modules/http/source/elasticsearch-bulk/libelasticsearch-bulk-source.la

modules_http_source_elasticsearch_bulk_libelasticsearch_bulk_source_la_SOURCES = \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-grammar.y \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-parser.c \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-parser.h \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-source.c \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-source.h

modules_http_source_elasticsearch_bulk_libelasticsearch_bulk_source_la_CPPFLAGS  = \
  $(AM_CPPFLAGS) \
  $(JSON_CFLAGS) \
  $(ZLIB_CFLAGS) \
  -I$(top_srcdir)/modules/http/source/elasticsearch-bulk \
  -I$(top_builddir)/modules/http/source/elasticsearch-bulk \
  -I$(top_srcdir)/modules/http/source \
  -I$(top_builddir)/modules/http/source

modules_http_source_elasticsearch_bulk_libelasticsearch_bulk_source_la_LIBADD  = $(MODULE_DEPS_LIBS) $(JSON_LIBS) $(ZLIB_LIBS)
modules_http_source_elasticsearch_bulk_libelasticsearch_bulk_source_la_LDFLAGS = $(MODULE_LDFLAGS)
modules_http_source_elasticsearch_bulk_libelasticsearch_bulk_source_la_DEPENDENCIES = $(MODULE_DEPS_LIBS) $(JSON_DEPENDENCY)

endif

BUILT_SOURCES += \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-grammar.y \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-grammar.c \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-grammar.h

EXTRA_DIST += \
  modules/http/source/elasticsearch-bulk/elasticsearch-bulk-grammar.ym \
  modules/http/source/elasticsearch-bulk/CMakeLists.txt

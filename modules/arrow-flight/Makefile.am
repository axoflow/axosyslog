if ENABLE_ARROW_FLIGHT

noinst_LTLIBRARIES += modules/arrow-flight/libarrow_flight_cpp.la

modules_arrow_flight_libarrow_flight_cpp_la_SOURCES = \
  modules/arrow-flight/arrow-flight-dest.h \
  modules/arrow-flight/arrow-flight-dest.hpp \
  modules/arrow-flight/arrow-flight-dest.cpp \
  modules/arrow-flight/arrow-flight-worker.h \
  modules/arrow-flight/arrow-flight-worker.hpp \
  modules/arrow-flight/arrow-flight-worker.cpp

modules_arrow_flight_libarrow_flight_cpp_la_CXXFLAGS = \
  $(AM_CXXFLAGS) \
  $(ARROW_FLIGHT_CFLAGS) \
  -I$(top_srcdir)/modules/arrow-flight \
  -I$(top_builddir)/modules/arrow-flight

modules_arrow_flight_libarrow_flight_cpp_la_LIBADD = $(MODULE_DEPS_LIBS) $(ARROW_FLIGHT_LIBS)
modules_arrow_flight_libarrow_flight_cpp_la_LDFLAGS = $(MODULE_LDFLAGS)
modules_arrow_flight_libarrow_flight_cpp_la_DEPENDENCIES = $(MODULE_DEPS_LIBS)


module_LTLIBRARIES += modules/arrow-flight/libarrow-flight.la

modules_arrow_flight_libarrow_flight_la_SOURCES = \
  modules/arrow-flight/arrow-flight-grammar.y \
  modules/arrow-flight/arrow-flight-parser.c \
  modules/arrow-flight/arrow-flight-parser.h \
  modules/arrow-flight/arrow-flight-plugin.c

modules_arrow_flight_libarrow_flight_la_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  -I$(top_srcdir)/modules/arrow-flight \
  -I$(top_builddir)/modules/arrow-flight

modules_arrow_flight_libarrow_flight_la_LIBADD = \
  $(MODULE_DEPS_LIBS) \
  $(top_builddir)/modules/arrow-flight/libarrow_flight_cpp.la \
  -lstdc++

modules_arrow_flight_libarrow_flight_la_LDFLAGS = $(MODULE_LDFLAGS)
modules_arrow_flight_libarrow_flight_la_DEPENDENCIES = \
  $(MODULE_DEPS_LIBS) \
  $(top_builddir)/modules/arrow-flight/libarrow_flight_cpp.la

modules/arrow-flight modules/arrow-flight/ mod-arrow-flight: modules/arrow-flight/libarrow-flight.la

else
modules/arrow-flight modules/arrow-flight/ mod-arrow-flight:
endif

BUILT_SOURCES += \
  modules/arrow-flight/arrow-flight-grammar.y \
  modules/arrow-flight/arrow-flight-grammar.c \
  modules/arrow-flight/arrow-flight-grammar.h

EXTRA_DIST += \
  modules/arrow-flight/arrow-flight-grammar.ym \
  modules/arrow-flight/CMakeLists.txt

.PHONY: modules/arrow-flight/ mod-arrow-flight

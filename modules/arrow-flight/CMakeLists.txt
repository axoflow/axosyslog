if (NOT DEFINED ENABLE_ARROW_FLIGHT OR ENABLE_ARROW_FLIGHT)
  set(ARROW_FLIGHT_DEPS_FOUND FALSE)
  message(STATUS "Apache Arrow Flight requires C++20, which is not supported by the compiler")
  find_package(Arrow QUIET CONFIG)
  if (Arrow_FOUND)
    find_package(ArrowFlight QUIET CONFIG)
    if (ArrowFlight_FOUND)
      set(ARROW_FLIGHT_DEPS_FOUND TRUE)
    else ()
      message(STATUS "Apache Arrow Flight library (libarrow-flight-dev) not found")
    endif ()
  else ()
    message(STATUS "Apache Arrow library (libarrow-dev >= 23.0.0) not found")
  endif ()
endif ()

module_switch(ENABLE_ARROW_FLIGHT "Enable Apache Arrow Flight module" ARROW_FLIGHT_DEPS_FOUND)

if (ENABLE_ARROW_FLIGHT AND NOT ARROW_FLIGHT_DEPS_FOUND)
  message(FATAL_ERROR "Arrow Flight enabled but required libraries not found (need libarrow >= 23.0.0, libarrow-flight >= 23.0.0, and a C++20-capable compiler)")
endif ()

if (NOT ENABLE_ARROW_FLIGHT)
  return()
endif ()

set(ARROW_FLIGHT_CPP_SOURCES
  arrow-flight-dest.h
  arrow-flight-dest.hpp
  arrow-flight-dest.cpp
  arrow-flight-worker.h
  arrow-flight-worker.hpp
  arrow-flight-worker.cpp
)

set(ARROW_FLIGHT_SOURCES
  arrow-flight-parser.h
  arrow-flight-plugin.c
  arrow-flight-parser.c
)

add_module(
  TARGET arrow-flight-cpp
  SOURCES ${ARROW_FLIGHT_CPP_SOURCES}
  DEPENDS ArrowFlight::arrow_flight_shared
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  LIBRARY_TYPE STATIC
)

add_module(
  TARGET arrow-flight
  GRAMMAR arrow-flight-grammar
  DEPENDS arrow-flight-cpp
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  SOURCES ${ARROW_FLIGHT_SOURCES}
)

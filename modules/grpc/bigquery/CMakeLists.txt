if(NOT ENABLE_GRPC)
  return()
endif()

set(BIGQUERY_CPP_SOURCES
  ${GRPC_METRICS_SOURCES}
  bigquery-dest.hpp
  bigquery-dest.cpp
  bigquery-dest.h
  bigquery-worker.hpp
  bigquery-worker.cpp
)

set(BIGQUERY_SOURCES
  bigquery-plugin.c
  bigquery-parser.c
  bigquery-parser.h
)

add_module(
  TARGET bigquery-cpp
  SOURCES ${BIGQUERY_CPP_SOURCES}
  DEPENDS ${MODULE_GRPC_LIBS} grpc-protos grpc-common-cpp
  INCLUDES ${BIGQUERY_PROTO_BUILDDIR} ${PROJECT_SOURCE_DIR}/modules/grpc
  LIBRARY_TYPE STATIC
)

add_module(
  TARGET bigquery
  GRAMMAR bigquery-grammar
  GRAMMAR_DEPENDS ${GRPC_COMMON_GRAMMAR}
  DEPENDS bigquery-cpp grpc-common-cpp
  INCLUDES ${PROJECT_SOURCE_DIR}/modules/grpc
  SOURCES ${BIGQUERY_SOURCES}
)

set_target_properties(bigquery PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib/syslog-ng")

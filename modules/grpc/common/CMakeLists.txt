add_subdirectory(credentials)
add_subdirectory(metrics)
add_subdirectory(schema)
add_subdirectory(filterx)

set(GRPC_COMMON_CPP_SOURCES
  ${GRPC_CREDENTIALS_SOURCES}
  ${GRPC_METRICS_SOURCES}
  ${GRPC_SCHEMA_SOURCES}
  ${GRPC_FILTERX_SOURCES}
  grpc-parser.h
  grpc-signals.h
  grpc-dest.hpp
  grpc-dest.cpp
  grpc-dest.h
  grpc-dest-worker.hpp
  grpc-dest-worker.cpp
  grpc-source.hpp
  grpc-source.cpp
  grpc-source.h
  grpc-grammar-internal.h
  grpc-grammar-internal.c
  grpc-source-worker.hpp
  grpc-source-worker.cpp)

set(GRPC_COMMON_GRAMMAR ${PROJECT_SOURCE_DIR}/modules/grpc/common/grpc-grammar.ym PARENT_SCOPE)

add_module(
  TARGET grpc-common-cpp
  SOURCES ${GRPC_COMMON_CPP_SOURCES}
  INCLUDES ${PROJECT_SOURCE_DIR}/modules/grpc/common
  LIBRARY_TYPE STATIC
)

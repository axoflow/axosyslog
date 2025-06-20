if(NOT ENABLE_GRPC)
  return()
endif()

set(GRPC_FILTERX_CPP_SOURCES
  func-protobuf-message.cpp
  func-protobuf-message.h
)

set(GRPC_FILTERX_SOURCES
  grpc-filterx-plugin.c
)

add_module(
  TARGET grpc-filterx-cpp
  SOURCES ${GRPC_FILTERX_CPP_SOURCES}
  DEPENDS ${MODULE_GRPC_LIBS} grpc-protos grpc-common-cpp
  INCLUDES ${PROJECT_SOURCE_DIR}/modules/grpc ${PROJECT_SOURCE_DIR}/modules/grpc/common
  LIBRARY_TYPE STATIC
)

add_module(
  TARGET grpc-filterx
  DEPENDS ${MODULE_GRPC_LIBS} grpc-filterx-cpp
  INCLUDES ${PROJECT_SOURCE_DIR}/modules/grpc ${PROJECT_SOURCE_DIR}/modules/grpc/common
  SOURCES ${GRPC_FILTERX_SOURCES}
)

set_target_properties(grpc-filterx PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib/syslog-ng")

add_test_subdirectory(tests)

if(NOT ENABLE_CPP)
  return()
endif()

# C++ code

set(PUBSUB_CPP_SOURCES
  object-pubsub.h
  object-pubsub-message.cpp
  object-pubsub-message.hpp
)

add_module(
  TARGET pubsub_filterx_message_cpp
  SOURCES ${PUBSUB_CPP_SOURCES}
  DEPENDS ${MODULE_GRPC_LIBS} grpc-protos
  INCLUDES ${GOOGLEAPIS_PROTO_GRPC_SOURCES} ${PROJECT_SOURCE_DIR}/modules/grpc
  LIBRARY_TYPE STATIC
)

set_target_properties(pubsub_filterx_message_cpp PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations")

add_unit_test(
  LIBTEST
  CRITERION
  TARGET test_protobuf_message
  SOURCES test_protobuf_message.c
  INCLUDES ${PROJECT_SOURCE_DIR}/modules/grpc/filterx
  DEPENDS grpc-filterx)

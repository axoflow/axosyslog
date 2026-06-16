if(NOT ENABLE_GRPC)
  return()
endif()

add_unit_test(
  CRITERION
  TARGET test_bigquery_worker
  SOURCES test-bigquery-worker.cpp
  INCLUDES ${BIGQUERY_PROTO_BUILDDIR} ${PROJECT_SOURCE_DIR}/modules/grpc ${PROJECT_SOURCE_DIR}/modules/grpc/bigquery
  DEPENDS bigquery-cpp)

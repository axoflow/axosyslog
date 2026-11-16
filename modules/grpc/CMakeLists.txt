
set(GRPC_DEPS_FOUND FALSE)

# TODO: Fix the remaining GRPC issues on APPLE
if (APPLE AND NOT DEFINED ENABLE_GRPC)
  set(ENABLE_GRPC FALSE)
endif ()

if (NOT DEFINED ENABLE_GRPC OR ENABLE_GRPC)
  set(GRPC_DEPS_FOUND TRUE)

  if (NOT ENABLE_CPP)
    if (ENABLE_GRPC)
      message(FATAL_ERROR "C++ support is mandatory when the GRPC modules are enabled.")
    endif()

    set(GRPC_DEPS_FOUND FALSE)
  endif()

  if (GRPC_DEPS_FOUND)
    find_package(Protobuf 3.6.1 QUIET)

    if (NOT Protobuf_FOUND)
      if (ENABLE_GRPC)
        message(FATAL_ERROR "ProtoBuf libraries not found.")
      endif()

      set(GRPC_DEPS_FOUND FALSE)
    else()
      message(STATUS "Found ProtoBuf: ${PROTOBUF_LIBRARIES}")
    endif()
  endif()

  if (GRPC_DEPS_FOUND)
    find_library(GRPC++_LIBRARIES NAMES grpc++)

    if (NOT GRPC++_LIBRARIES)
      if (ENABLE_GRPC)
        message(FATAL_ERROR "gRPC++ libraries not found.")
      endif()

      set(GRPC_DEPS_FOUND FALSE)
    else()
      message(STATUS "Found gRPC++: ${GRPC++_LIBRARIES}")
    endif()
  endif()

  if (GRPC_DEPS_FOUND)
    find_package(gRPC 1.16.1 QUIET)

    if (NOT gRPC_FOUND)
      if (ENABLE_GRPC)
        message(FATAL_ERROR "gRPC libraries not found.")
      endif()

      set(GRPC_DEPS_FOUND FALSE)
    else()
      message(STATUS "Found gRPC: ${GRPC_LIBRARIES}")
    endif()
  endif()
endif()

module_switch(ENABLE_GRPC "Enable GRPC" GRPC_DEPS_FOUND)

if (ENABLE_GRPC)
  include(ProtobufGenerateCpp)

  set(CMAKE_REQUIRED_LIBRARIES gRPC::grpc++)
  set(CMAKE_EXTRA_INCLUDE_FILES grpcpp/security/tls_certificate_provider.h)
  check_type_size(grpc::experimental::InMemoryCertificateProvider GRPC_IN_MEMORY_CERTIFICATE_PROVIDER LANGUAGE CXX)
  set(SYSLOG_NG_HAVE_GRPC_IN_MEMORY_CERTIFICATE_PROVIDER ${HAVE_GRPC_IN_MEMORY_CERTIFICATE_PROVIDER} CACHE INTERNAL "")

  set(MODULE_GRPC_LIBS
    gRPC::grpc
    gRPC::grpc++
    protobuf::libprotobuf)

  add_subdirectory(common)
  add_subdirectory(protos)
endif()

# These are intentionally not inside the above if (ENABLE_GRPC) block
# Let any other possible module_switch-es take effect and is being always visible
# (both modules are protected via ENABLE_GRPC as well)
add_subdirectory(filterx)
add_subdirectory(loki)
add_subdirectory(otel)
add_subdirectory(bigquery)
add_subdirectory(pubsub)
add_subdirectory(clickhouse)

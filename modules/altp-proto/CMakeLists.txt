set(ALTP_PROTO_SOURCES
    altp-session.c
    altp-session.h
    altp-proto-options.c
    altp-proto-options.h
    altp-proto-parser.c
    altp-proto-parser.h
    altp-proto-client-parser.c
    altp-proto-client-parser.h
    altp-proto-plugin.c
    logproto-altp-client.c
    logproto-altp-client.h
    logproto-altp-server.c
    logproto-altp-server.h
)

add_module(
  TARGET altp-proto
  GRAMMAR altp-proto-grammar
  SOURCES ${ALTP_PROTO_SOURCES}
)

# The Sender has a grammar of its own, and the GRAMMAR argument of add_module()
# takes a single one, so the second grammar is generated the very same way here
# and added to the module.
module_generate_y_from_ym(
  ${CMAKE_CURRENT_SOURCE_DIR}/altp-proto-client-grammar
  ${CMAKE_CURRENT_BINARY_DIR}/altp-proto-client-grammar
)
bison_target(AltpProtoClientGrammar
  ${CMAKE_CURRENT_BINARY_DIR}/altp-proto-client-grammar.y
  ${CMAKE_CURRENT_BINARY_DIR}/altp-proto-client-grammar.c
  COMPILE_FLAGS ${BISON_FLAGS}
)
set_source_files_properties(
  ${CMAKE_CURRENT_BINARY_DIR}/altp-proto-client-grammar.c
  PROPERTIES
  COMPILE_FLAGS ${BISON_BUILT_SOURCE_CFLAGS}
)
target_sources(altp-proto PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/altp-proto-client-grammar.c
  ${CMAKE_CURRENT_BINARY_DIR}/altp-proto-client-grammar.h
)

# a Session ID is 128 bits from a cryptographic random source (specification 15)
target_include_directories(altp-proto SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(altp-proto PRIVATE ${OPENSSL_LIBRARIES})

add_test_subdirectory(tests)

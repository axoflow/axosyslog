set(LUMBERJACK_PROTO_SOURCES
    lumberjack-proto-options.c
    lumberjack-proto-options.h
    lumberjack-proto-parser.c
    lumberjack-proto-parser.h
    lumberjack-proto-plugin.c
    logproto-lumberjack-server.c
    logproto-lumberjack-server.h
)

add_module(
  TARGET lumberjack-proto
  GRAMMAR lumberjack-proto-grammar
  SOURCES ${LUMBERJACK_PROTO_SOURCES}
)

add_test_subdirectory(tests)

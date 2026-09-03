set(ALTP_PROTO_SOURCES
    altp-session.c
    altp-session.h
    altp-proto-options.c
    altp-proto-options.h
    altp-proto-parser.c
    altp-proto-parser.h
    altp-proto-plugin.c
    logproto-altp-server.c
    logproto-altp-server.h
)

add_module(
  TARGET altp-proto
  GRAMMAR altp-proto-grammar
  SOURCES ${ALTP_PROTO_SOURCES}
)

add_test_subdirectory(tests)

set(HTTP_HEADERS
    http/buffer.h
    http/http-status.h
    http/http-message.h
    http/http-message-internal.h
    PARENT_SCOPE)

set(HTTP_SOURCES
    http/http-message.c
    PARENT_SCOPE)

add_test_subdirectory(tests)

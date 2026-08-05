set(HTTP_HEADERS
    http/buffer.h
    http/http-status.h
    http/http-message.h
    http/http-message-internal.h
    http/http-parser.h
    PARENT_SCOPE)

set(HTTP_SOURCES
    http/http-message.c
    http/http-parser.c
    http/llhttp/src/llhttp.c
    http/llhttp/src/api.c
    http/llhttp/src/http.c
    PARENT_SCOPE)

add_test_subdirectory(tests)

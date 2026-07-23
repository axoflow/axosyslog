module_switch(ENABLE_HTTP_SOURCE "Enable http source")
if (NOT ENABLE_HTTP_SOURCE)
  return ()
endif ()

set(HTTP_SOURCE_SOURCES
    http-source.h
    http-source.c
    http-server-listener.h
    http-server-listener.c
    http-source-config.h
    http-source-config.c
    http-source-parser.c
    http-source-parser.h
    http-source-plugin.c
    llhttp/llhttp.c
    llhttp/api.c
    llhttp/http.c
)

# llhttp is vendored, pre-generated code; do not warn about it
set_source_files_properties(
    llhttp/llhttp.c llhttp/api.c llhttp/http.c
    PROPERTIES COMPILE_OPTIONS "-w"
)

add_module(
  TARGET http-source
  GRAMMAR http-source-grammar
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/llhttp
  SOURCES ${HTTP_SOURCE_SOURCES}
)

# only compile this if the HTTP module (masquerading as the CURL module)
# is enabled

if (NOT ENABLE_CURL)
  return ()
endif ()

set(HTTP_ADAPTERS_SOURCES
    http-adapter.c
    http-adapter.h
    http-adapters-parser.c
    http-adapters-parser.h
    http-adapters-plugin.c
    splunk-adapter.c
    splunk-adapter.h
)

add_module(
  TARGET http-adapters
  GRAMMAR http-adapters-grammar
  SOURCES ${HTTP_ADAPTERS_SOURCES}
)

add_test_subdirectory(tests)

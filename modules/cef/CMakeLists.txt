set(CEF_SOURCES
    format-cef-extension.c
    format-cef-extension.h
    cef-plugin.c
    event-format-parser.c
    event-format-parser.h
    event-format-formatter.c
    event-format-formatter.h
    event-format-cfg.h
    filterx-func-parse-cef.c
    filterx-func-parse-cef.h
    filterx-func-parse-leef.c
    filterx-func-parse-leef.h
)

add_module(
  TARGET cef
  SOURCES ${CEF_SOURCES}
)

add_test_subdirectory(tests)

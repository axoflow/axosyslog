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
    filterx-func-format-cef.c
    filterx-func-format-cef.h
    filterx-func-format-leef.c
    filterx-func-format-leef.h
)

add_module(
  TARGET cef
  SOURCES ${CEF_SOURCES}
)

add_test_subdirectory(tests)

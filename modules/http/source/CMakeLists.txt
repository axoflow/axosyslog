add_library(ehttp-source STATIC
  http-source-parser.c
  http-source-parser.h
  http-source.c
  http-source.h
  ${CMAKE_CURRENT_BINARY_DIR}/http-source-grammar.c
  ${CMAKE_CURRENT_BINARY_DIR}/http-source-grammar.h
)

target_link_libraries(ehttp-source PUBLIC syslog-ng)
target_include_directories(ehttp-source
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
)

generate_y_from_ym(modules/http/source/http-source-grammar)
bison_target(EHTTPSourceGrammar
    ${CMAKE_CURRENT_BINARY_DIR}/http-source-grammar.y
    ${CMAKE_CURRENT_BINARY_DIR}/http-source-grammar.c
    COMPILE_FLAGS ${BISON_FLAGS})
set_source_files_properties(
    ${CMAKE_CURRENT_BINARY_DIR}/http-source-grammar.c
    PROPERTIES
    COMPILE_FLAGS ${BISON_BUILT_SOURCE_CFLAGS}
)

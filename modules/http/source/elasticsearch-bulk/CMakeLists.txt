add_library(elasticsearch-bulk-source STATIC
  elasticsearch-bulk-parser.c
  elasticsearch-bulk-parser.h
  elasticsearch-bulk-source.c
  elasticsearch-bulk-source.h
  ${CMAKE_CURRENT_BINARY_DIR}/elasticsearch-bulk-grammar.c
  ${CMAKE_CURRENT_BINARY_DIR}/elasticsearch-bulk-grammar.h
)

target_link_libraries(elasticsearch-bulk-source PUBLIC syslog-ng http-decompression)
target_include_directories(elasticsearch-bulk-source
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
)

generate_y_from_ym(modules/http/source/elasticsearch-bulk/elasticsearch-bulk-grammar)
bison_target(ESBulkSourceGrammar
    ${CMAKE_CURRENT_BINARY_DIR}/elasticsearch-bulk-grammar.y
    ${CMAKE_CURRENT_BINARY_DIR}/elasticsearch-bulk-grammar.c
    COMPILE_FLAGS ${BISON_FLAGS})
set_source_files_properties(
    ${CMAKE_CURRENT_BINARY_DIR}/elasticsearch-bulk-grammar.c
    PROPERTIES
    COMPILE_FLAGS ${BISON_BUILT_SOURCE_CFLAGS}
)

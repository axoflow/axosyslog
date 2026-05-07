add_subdirectory(csv-scanner)
add_subdirectory(list-scanner)
add_subdirectory(kv-scanner)
add_subdirectory(xml-scanner)

# Re-base each scanner's source/header list under "scanner/".
set(_all_sources
    ${CSV_SCANNER_SOURCES}
    ${KV_SCANNER_SOURCES}
    ${LIST_SCANNER_SOURCES}
    ${XML_SCANNER_SOURCES})
list(TRANSFORM _all_sources PREPEND "scanner/")

set(_all_headers
    ${CSV_SCANNER_HEADERS}
    ${KV_SCANNER_HEADERS}
    ${LIST_SCANNER_HEADERS}
    ${XML_SCANNER_HEADERS})
list(TRANSFORM _all_headers PREPEND "scanner/")

set(SCANNER_SOURCES ${_all_sources} PARENT_SCOPE)
set(SCANNER_HEADERS ${_all_headers} PARENT_SCOPE)

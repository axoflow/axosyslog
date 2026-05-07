set(CSV_SCANNER_HEADERS
    csv-scanner/csv-scanner.h
    csv-scanner/csv-scanner-simd.h
    PARENT_SCOPE)

set(CSV_SCANNER_SOURCES
    csv-scanner/csv-scanner.c
    csv-scanner/csv-scanner-simd.c
    PARENT_SCOPE)

set(CSV_SCANNER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

add_test_subdirectory(tests)

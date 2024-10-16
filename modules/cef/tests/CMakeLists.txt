set(TEST_HELPER_SOURCES
    test_helpers.c
    test_helpers.h
    )

add_unit_test(LIBTEST CRITERION TARGET test-format-cef-extension DEPENDS cef)
add_unit_test(LIBTEST CRITERION TARGET test-filterx-function-parse-cef DEPENDS cef SOURCES test-filterx-function-parse-cef.c ${TEST_HELPER_SOURCES})
add_unit_test(LIBTEST CRITERION TARGET test-filterx-function-parse-leef DEPENDS cef SOURCES test-filterx-function-parse-leef.c ${TEST_HELPER_SOURCES})

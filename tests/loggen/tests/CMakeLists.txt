add_unit_test(CRITERION LIBTEST TARGET test_loggen_filereader DEPENDS loggen_helper)
target_include_directories(test_loggen_filereader PUBLIC
  ${PROJECT_SOURCE_DIR}
  )

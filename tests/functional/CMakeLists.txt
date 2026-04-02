add_custom_target(func-test
  ${CMAKE_COMMAND} -E env "top_srcdir=${PROJECT_SOURCE_DIR}" "top_builddir=${PROJECT_BINARY_DIR}" "PYTHON=${PYTHON_EXECUTABLE}" "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tests/axosyslog-lsan.supp"
  "${CMAKE_CURRENT_SOURCE_DIR}/runtests.sh")

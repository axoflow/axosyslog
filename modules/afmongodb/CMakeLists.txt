if (ENABLE_MONGODB STREQUAL OFF)
  return()
endif()

find_package(libmongoc-1.0)
find_package(libbson-1.0)

module_switch(ENABLE_MONGODB "Enable mongodb destination driver" libmongoc-1.0_FOUND)

if (ENABLE_MONGODB)

  set(CMAKE_REQUIRED_INCLUDES ${MONGOC_INCLUDE_DIRS})
  set(CMAKE_REQUIRED_LIBRARIES ${MONGOC_LIBRARY})
  set(CMAKE_REQUIRED_LIBRARIES ${BSON_LIBRARY})
  check_symbol_exists(mongoc_uri_set_option_as_int32 "mongoc.h" SYSLOG_NG_HAVE_DECL_MONGOC_URI_SET_OPTION_AS_INT32)
  check_symbol_exists(MONGOC_URI_SERVERSELECTIONTIMEOUTMS "mongoc.h" SYSLOG_NG_HAVE_DECL_MONGOC_URI_SERVERSELECTIONTIMEOUTMS)

  check_c_source_compiles("
#include <mongoc/mongoc.h>
#include <bson/bson.h>
int main() {
    (void) bson_append_array_unsafe_begin;
    return 0;
}" HAVE_BSON_APPEND_ARRAY_UNSAFE_BEGIN)

if(HAVE_BSON_APPEND_ARRAY_UNSAFE_BEGIN)
  set(SYSLOG_NG_HAVE_DECL_BSON_APPEND_ARRAY_UNSAFE_BEGIN 1 CACHE INTERNAL "")
endif()

  set(AFMONGODB_SOURCES
    "afmongodb.h"
    "afmongodb.c"
    "afmongodb-worker.h"
    "afmongodb-worker.c"
    "afmongodb-parser.h"
    "afmongodb-parser.c"
    "afmongodb-private.h"
  )

  add_module(
    TARGET afmongodb
    GRAMMAR afmongodb-grammar
    INCLUDES ${MONGOC_INCLUDE_DIRS}
    DEPENDS ${MONGOC_LIBRARY}
    DEPENDS ${BSON_LIBRARY}
    SOURCES ${AFMONGODB_SOURCES}
  )

  add_test_subdirectory(tests)

endif()

set(SPLUNK_S2S_PROTOCOL_SOURCES
    splunk-s2s-protocol.c
    splunk-s2s-protocol.h
)

add_module(
  TARGET splunk-s2s-protocol
  SOURCES ${SPLUNK_S2S_PROTOCOL_SOURCES}
  LIBRARY_TYPE STATIC # STATIC: built as part of the larger libsplunk-s2s module
)

target_include_directories(splunk-s2s-protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_test_subdirectory(tests)

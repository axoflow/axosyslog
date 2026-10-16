add_executable(filterx-tool filterx-tool.c)

target_link_libraries(filterx-tool PRIVATE syslog-ng)

install(TARGETS filterx-tool RUNTIME DESTINATION bin)

add_unit_test(CRITERION TARGET test_xml_parser DEPENDS xml syslog-ng)
add_unit_test(CRITERION TARGET test_windows_eventlog_xml_parser DEPENDS xml syslog-ng)
add_unit_test(LIBTEST CRITERION TARGET test_filterx_parse_xml DEPENDS xml syslog-ng)

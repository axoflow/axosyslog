add_unit_test(CRITERION TARGET test_splunk_s2s_protocol DEPENDS splunk-s2s-protocol)
add_unit_test(CRITERION LIBTEST TARGET test_splunk_s2s_proto_client DEPENDS splunk-s2s-client splunk-s2s-protocol)

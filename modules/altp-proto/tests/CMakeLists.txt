add_unit_test(LIBTEST CRITERION TARGET test_altp_proto DEPENDS altp-proto afsocket)
add_unit_test(LIBTEST CRITERION TARGET test_altp_client DEPENDS altp-proto afsocket)

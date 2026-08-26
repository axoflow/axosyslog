add_unit_test(LIBTEST CRITERION
  TARGET test_geoip2_parser
  INCLUDES "${GEOIP2_INCLUDE_DIR}"
  DEPENDS geoip2-plugin
  SOURCES test_geoip_parser.c)

add_unit_test(LIBTEST CRITERION
  TARGET test_filterx_func_geoip
  INCLUDES "${GEOIP2_INCLUDE_DIR}"
  DEPENDS geoip2-plugin
  SOURCES test_filterx_func_geoip.c)

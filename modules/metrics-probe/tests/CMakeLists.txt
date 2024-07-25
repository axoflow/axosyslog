add_unit_test(CRITERION TARGET test_metrics_probe DEPENDS metrics-probe)
add_unit_test(LIBTEST CRITERION TARGET test_filterx_func_update_metric DEPENDS metrics-probe)

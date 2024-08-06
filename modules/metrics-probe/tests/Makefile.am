modules_metrics_probe_tests_TESTS			=	\
	modules/metrics-probe/tests/test_metrics_probe \
	modules/metrics-probe/tests/test_filterx_func_update_metric

check_PROGRAMS					+=	\
	${modules_metrics_probe_tests_TESTS}

EXTRA_DIST += \
	modules/metrics-probe/tests/CMakeLists.txt \
	modules/metrics-probe/tests/metrics-probe-test.h

modules_metrics_probe_tests_test_metrics_probe_CFLAGS	=	\
	$(TEST_CFLAGS) \
	-I$(top_srcdir)/modules/metrics-probe \
	-I$(top_srcdir)/modules/metrics-probe/tests
modules_metrics_probe_tests_test_metrics_probe_LDADD	=	\
	$(TEST_LDADD)					\
	-dlpreopen $(top_builddir)/modules/metrics-probe/libmetrics-probe.la

modules_metrics_probe_tests_test_filterx_func_update_metric_CFLAGS	=	\
	$(TEST_CFLAGS) \
	-I$(top_srcdir)/modules/metrics-probe \
	-I$(top_srcdir)/modules/metrics-probe/tests
modules_metrics_probe_tests_test_filterx_func_update_metric_LDADD	=	\
	$(TEST_LDADD)					\
	-dlpreopen $(top_builddir)/modules/metrics-probe/libmetrics-probe.la

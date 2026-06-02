check_PROGRAMS += \
	${modules_afuser_tests_TESTS}

modules_afuser_tests_TESTS = \
	modules/afuser/tests/test_afuser

modules_afuser_tests_test_afuser_CFLAGS = $(TEST_CFLAGS) -I$(top_srcdir)/modules/afuser
modules_afuser_tests_test_afuser_LDADD = $(TEST_LDADD)
modules_afuser_tests_test_afuser_LDFLAGS = \
	-dlpreopen $(top_builddir)/modules/afuser/libafuser.la

EXTRA_DIST += modules/afuser/tests/CMakeLists.txt

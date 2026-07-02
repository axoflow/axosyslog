lib_filterx_tests_TESTS		 =              \
		lib/filterx/tests/test_object_primitive	\
		lib/filterx/tests/test_object_message	\
		lib/filterx/tests/test_object_datetime	\
		lib/filterx/tests/test_object_dict	\
		lib/filterx/tests/test_object_tuple	\
		lib/filterx/tests/test_object_null	\
		lib/filterx/tests/test_object_string	\
		lib/filterx/tests/test_object_protobuf	\
		lib/filterx/tests/test_object_double	\
		lib/filterx/tests/test_object_boolean	\
		lib/filterx/tests/test_object_integer	\
		lib/filterx/tests/test_object_bytes	\
		lib/filterx/tests/test_object_cow	\
		lib/filterx/tests/test_filterx_expr	\
		lib/filterx/tests/test_filterx_plist	\
		lib/filterx/tests/test_expr_function	\
		lib/filterx/tests/test_expr_comparison \
		lib/filterx/tests/test_expr_condition \
		lib/filterx/tests/test_expr_compound \
		lib/filterx/tests/test_expr_function \
		lib/filterx/tests/test_builtin_functions \
		lib/filterx/tests/test_type_registry \
		lib/filterx/tests/test_func_istype \
		lib/filterx/tests/test_func_unset_empties \
		lib/filterx/tests/test_expr_move \
		lib/filterx/tests/test_func_flatten \
		lib/filterx/tests/test_expr_regexp_search	\
		lib/filterx/tests/test_expr_regexp_subst	\
		lib/filterx/tests/test_expr_regexp	\
		lib/filterx/tests/test_expr_null_coalesce	\
		lib/filterx/tests/test_expr_plus	\
		lib/filterx/tests/test_expr_plus \
		lib/filterx/tests/test_metrics_labels \
		lib/filterx/tests/test_object_dict_interface \
		lib/filterx/tests/test_func_keys \
		lib/filterx/tests/test_func_format_json \
		lib/filterx/tests/test_scope \
		lib/filterx/tests/test_filterx_eval \
		lib/filterx/tests/test_func_uuid \
		lib/filterx/tests/test_func_random \
		lib/filterx/tests/test_func_digest \
		lib/filterx/tests/test_func_encode \
		lib/filterx/tests/test_func_str_utf8 \
		lib/filterx/tests/test_func_str_affix \
		lib/filterx/tests/test_func_str_strcasecmp \
		lib/filterx/tests/test_func_glob \
		lib/filterx/tests/test_object_subnet \
		lib/filterx/tests/test_object_ip \
		lib/filterx/tests/test_expr_arithmetic_operators \
		lib/filterx/tests/test_expr_membership \
		lib/filterx/tests/test_func_set_pri \
		lib/filterx/tests/test_json_repr \
		lib/filterx/tests/test_access_path \
		lib/filterx/tests/test_type_inference

EXTRA_DIST += lib/filterx/tests/CMakeLists.txt

check_PROGRAMS				+= ${lib_filterx_tests_TESTS}

lib_filterx_tests_test_object_primitive_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_primitive_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_message_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_message_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_datetime_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_datetime_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_dict_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_dict_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_tuple_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_tuple_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_null_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_null_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_string_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_string_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_filterx_expr_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_filterx_expr_LDADD   = $(TEST_LDADD) $(PREOPEN_SYSLOGFORMAT) $(JSON_LIBS)

lib_filterx_tests_test_access_path_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_access_path_LDADD   = $(TEST_LDADD) $(PREOPEN_SYSLOGFORMAT) $(JSON_LIBS)

lib_filterx_tests_test_type_inference_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_type_inference_LDADD   = $(TEST_LDADD) $(PREOPEN_SYSLOGFORMAT) $(JSON_LIBS)

lib_filterx_tests_test_filterx_plist_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_filterx_plist_LDADD   = $(TEST_LDADD) $(PREOPEN_SYSLOGFORMAT) $(JSON_LIBS)

lib_filterx_tests_test_expr_comparison_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_comparison_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_condition_CFLAGS = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_condition_LDADD	 = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_compound_CFLAGS = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_compound_LDADD	 = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_builtin_functions_CFLAGS = $(TEST_CFLAGS)
lib_filterx_tests_test_builtin_functions_LDADD = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_bytes_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_bytes_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_cow_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_cow_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_protobuf_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_protobuf_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_boolean_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_boolean_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_integer_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_integer_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_double_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_double_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_type_registry_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_type_registry_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_istype_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_istype_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_unset_empties_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_unset_empties_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_move_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_move_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_flatten_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_flatten_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_function_CFLAGS	= $(TEST_CFLAGS)
lib_filterx_tests_test_expr_function_LDADD	= $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_regexp_search_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_regexp_search_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_regexp_subst_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_regexp_subst_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_regexp_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_regexp_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_null_coalesce_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_null_coalesce_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_plus_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_plus_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_metrics_labels_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_metrics_labels_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_dict_interface_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_dict_interface_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_keys_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_keys_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_scope_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_scope_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_format_json_CFLAGS	= $(TEST_CFLAGS)
lib_filterx_tests_test_func_format_json_LDADD	= $(TEST_LDADD)

lib_filterx_tests_test_filterx_eval_CFLAGS	= $(TEST_CFLAGS)
lib_filterx_tests_test_filterx_eval_LDADD	= $(TEST_LDADD)

lib_filterx_tests_test_func_uuid_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_uuid_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_random_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_random_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_digest_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_digest_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_encode_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_encode_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_str_utf8_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_str_utf8_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_str_affix_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_str_affix_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_str_strcasecmp_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_str_strcasecmp_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_glob_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_glob_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_subnet_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_subnet_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_object_ip_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_object_ip_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_arithmetic_operators_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_arithmetic_operators_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_expr_membership_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_expr_membership_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_func_set_pri_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_func_set_pri_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

lib_filterx_tests_test_json_repr_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_test_json_repr_LDADD   = $(TEST_LDADD) $(JSON_LIBS)

# Not check_PROGRAMS: libFuzzer supplies main(), and these must not run
# under `make check`.
if ENABLE_FUZZING
noinst_PROGRAMS += lib/filterx/tests/fuzz/fuzz_parse_json
noinst_PROGRAMS += lib/filterx/tests/fuzz/fuzz_regexp
noinst_PROGRAMS += lib/filterx/tests/fuzz/fuzz_decode
noinst_PROGRAMS += lib/filterx/tests/fuzz/fuzz_typecast
noinst_PROGRAMS += lib/filterx/tests/fuzz/fuzz_str
noinst_PROGRAMS += lib/filterx/tests/fuzz/fuzz_parse_xml
noinst_PROGRAMS += lib/filterx/tests/fuzz/fuzz_parse_cef

lib_filterx_tests_fuzz_fuzz_parse_json_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_fuzz_fuzz_parse_json_LDFLAGS = -fsanitize=fuzzer
lib_filterx_tests_fuzz_fuzz_parse_json_LDADD   = $(top_builddir)/lib/libsyslog-ng.la $(TOOL_DEPS_LIBS) $(JSON_LIBS)

lib_filterx_tests_fuzz_fuzz_regexp_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_fuzz_fuzz_regexp_LDFLAGS = -fsanitize=fuzzer
lib_filterx_tests_fuzz_fuzz_regexp_LDADD   = $(top_builddir)/lib/libsyslog-ng.la $(TOOL_DEPS_LIBS) $(JSON_LIBS)

lib_filterx_tests_fuzz_fuzz_decode_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_fuzz_fuzz_decode_LDFLAGS = -fsanitize=fuzzer
lib_filterx_tests_fuzz_fuzz_decode_LDADD   = $(top_builddir)/lib/libsyslog-ng.la $(TOOL_DEPS_LIBS) $(JSON_LIBS)

lib_filterx_tests_fuzz_fuzz_typecast_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_fuzz_fuzz_typecast_LDFLAGS = -fsanitize=fuzzer
lib_filterx_tests_fuzz_fuzz_typecast_LDADD   = $(top_builddir)/lib/libsyslog-ng.la $(TOOL_DEPS_LIBS) $(JSON_LIBS)

lib_filterx_tests_fuzz_fuzz_str_CFLAGS  = $(TEST_CFLAGS)
lib_filterx_tests_fuzz_fuzz_str_LDFLAGS = -fsanitize=fuzzer
lib_filterx_tests_fuzz_fuzz_str_LDADD   = $(top_builddir)/lib/libsyslog-ng.la $(TOOL_DEPS_LIBS) $(JSON_LIBS)

lib_filterx_tests_fuzz_fuzz_parse_xml_CFLAGS  = $(TEST_CFLAGS) -I$(top_srcdir)/modules/xml
lib_filterx_tests_fuzz_fuzz_parse_xml_LDFLAGS = -fsanitize=fuzzer \
	-dlpreopen $(top_builddir)/modules/xml/libxml.la
lib_filterx_tests_fuzz_fuzz_parse_xml_LDADD   = $(top_builddir)/lib/libsyslog-ng.la $(TOOL_DEPS_LIBS) $(JSON_LIBS)
EXTRA_lib_filterx_tests_fuzz_fuzz_parse_xml_DEPENDENCIES = $(top_builddir)/modules/xml/libxml.la

lib_filterx_tests_fuzz_fuzz_parse_cef_CFLAGS  = $(TEST_CFLAGS) -I$(top_srcdir)/modules/cef
lib_filterx_tests_fuzz_fuzz_parse_cef_LDFLAGS = -fsanitize=fuzzer \
	-dlpreopen $(top_builddir)/modules/cef/libcef.la
lib_filterx_tests_fuzz_fuzz_parse_cef_LDADD   = $(top_builddir)/lib/libsyslog-ng.la $(TOOL_DEPS_LIBS) $(JSON_LIBS)
EXTRA_lib_filterx_tests_fuzz_fuzz_parse_cef_DEPENDENCIES = $(top_builddir)/modules/cef/libcef.la
endif

/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Tamás Kosztyu <tamas.kosztyu@axoflow.com>
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include "filterx/filterx-globals.h"
#include "filterx/filterx-private.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-datetime.h"
#include "filterx/object-message-value.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-metrics-labels.h"
#include "filterx/func-istype.h"
#include "filterx/func-len.h"
#include "filterx/func-vars.h"
#include "filterx/func-unset-empties.h"
#include "filterx/func-set-fields.h"
#include "filterx/func-set-pri.h"
#include "filterx/func-timestamp.h"
#include "filterx/func-str.h"
#include "filterx/func-str-transform.h"
#include "filterx/func-flatten.h"
#include "filterx/func-sdata.h"
#include "filterx/func-repr.h"
#include "filterx/func-cache-json-file.h"
#include "filterx/func-failure-info.h"
#include "filterx/func-dict-to-pairs.h"
#include "filterx/expr-regexp-search.h"
#include "filterx/expr-regexp-subst.h"
#include "filterx/expr-regexp.h"
#include "filterx/expr-unset.h"
#include "filterx/filterx-eval.h"
#include "filterx/func-keys.h"
#include "filterx/json-repr.h"

static GHashTable *filterx_builtin_simple_functions = NULL;
static GHashTable *filterx_builtin_function_ctors = NULL;
static GHashTable *filterx_types = NULL;


gboolean
filterx_builtin_simple_function_register(const gchar *fn_name, FilterXSimpleFunctionProto func)
{
  return filterx_builtin_simple_function_register_private(filterx_builtin_simple_functions, fn_name, func);
}

FilterXSimpleFunctionProto
filterx_builtin_simple_function_lookup(const gchar *fn_name)
{
  return filterx_builtin_simple_function_lookup_private(filterx_builtin_simple_functions, fn_name);
}

gboolean
filterx_builtin_function_exists(const gchar *function_name)
{
  return !!filterx_builtin_simple_function_lookup(function_name) ||
         !!filterx_builtin_function_ctor_lookup(function_name);
}

static void
_simple_init(void)
{
  filterx_builtin_simple_functions_init_private(&filterx_builtin_simple_functions);
  g_assert(filterx_builtin_simple_function_register("dict", filterx_dict_new_from_args));
  g_assert(filterx_builtin_simple_function_register("list", filterx_list_new_from_args));
  g_assert(filterx_builtin_simple_function_register("json", filterx_dict_new_from_args));
  g_assert(filterx_builtin_simple_function_register("json_array", filterx_list_new_from_args));
  g_assert(filterx_builtin_simple_function_register("format_json", filterx_format_json_call));
  g_assert(filterx_builtin_simple_function_register("parse_json", filterx_parse_json_call));
  g_assert(filterx_builtin_simple_function_register("datetime", filterx_typecast_datetime));
  g_assert(filterx_builtin_simple_function_register("isodate", filterx_typecast_datetime_isodate));
  g_assert(filterx_builtin_simple_function_register("string", filterx_typecast_string));
  g_assert(filterx_builtin_simple_function_register("repr", filterx_simple_function_repr));
  g_assert(filterx_builtin_simple_function_register("bytes", filterx_typecast_bytes));
  g_assert(filterx_builtin_simple_function_register("protobuf", filterx_typecast_protobuf));
  g_assert(filterx_builtin_simple_function_register("bool", filterx_typecast_boolean));
  g_assert(filterx_builtin_simple_function_register("int", filterx_typecast_integer));
  g_assert(filterx_builtin_simple_function_register("double", filterx_typecast_double));
  g_assert(filterx_builtin_simple_function_register("metrics_labels", filterx_simple_function_metrics_labels));
  g_assert(filterx_builtin_simple_function_register("dedup_metrics_labels",
                                                    filterx_simple_function_dedup_metrics_labels));
  g_assert(filterx_builtin_simple_function_register("len", filterx_simple_function_len));
  g_assert(filterx_builtin_simple_function_register("load_vars", filterx_simple_function_load_vars));
  g_assert(filterx_builtin_simple_function_register("lower", filterx_simple_function_lower));
  g_assert(filterx_builtin_simple_function_register("upper", filterx_simple_function_upper));
  g_assert(filterx_builtin_simple_function_register("str_strip", filterx_simple_function_str_strip));
  g_assert(filterx_builtin_simple_function_register("str_lstrip", filterx_simple_function_str_lstrip));
  g_assert(filterx_builtin_simple_function_register("str_rstrip", filterx_simple_function_str_rstrip));
  g_assert(filterx_builtin_simple_function_register("str_replace", filterx_simple_function_str_replace));
  g_assert(filterx_builtin_simple_function_register("has_sdata",
                                                    filterx_simple_function_has_sdata));
  g_assert(filterx_builtin_simple_function_register("get_sdata",
                                                    filterx_simple_function_get_sdata));
}

static void
_simple_deinit(void)
{
  filterx_builtin_simple_functions_deinit_private(filterx_builtin_simple_functions);
}

static gboolean
filterx_builtin_function_ctor_register(const gchar *fn_name, FilterXFunctionCtor ctor)
{
  return filterx_builtin_function_ctor_register_private(filterx_builtin_function_ctors, fn_name, ctor);
}

FilterXFunctionCtor
filterx_builtin_function_ctor_lookup(const gchar *function_name)
{
  return filterx_builtin_function_ctor_lookup_private(filterx_builtin_function_ctors, function_name);
}

static void
_ctors_init(void)
{
  filterx_builtin_function_ctors_init_private(&filterx_builtin_function_ctors);
  g_assert(filterx_builtin_function_ctor_register("strptime", filterx_function_strptime_new));
  g_assert(filterx_builtin_function_ctor_register("istype", filterx_function_istype_new));
  g_assert(filterx_builtin_function_ctor_register("unset_empties", filterx_function_unset_empties_new));
  g_assert(filterx_builtin_function_ctor_register("set_fields", filterx_function_set_fields_new));
  g_assert(filterx_builtin_function_ctor_register("regexp_subst", filterx_function_regexp_subst_new));
  g_assert(filterx_builtin_function_ctor_register("unset", filterx_function_unset_new));
  g_assert(filterx_builtin_function_ctor_register("flatten", filterx_function_flatten_new));
  g_assert(filterx_builtin_function_ctor_register("is_sdata_from_enterprise",
                                                  filterx_function_is_sdata_from_enterprise_new));
  g_assert(filterx_builtin_function_ctor_register("startswith", filterx_function_startswith_new));
  g_assert(filterx_builtin_function_ctor_register("endswith", filterx_function_endswith_new));
  g_assert(filterx_builtin_function_ctor_register("includes", filterx_function_includes_new));
  g_assert(filterx_builtin_function_ctor_register("strcasecmp", filterx_function_strcasecmp_new));
  g_assert(filterx_builtin_function_ctor_register("strftime", filterx_function_strftime_new));
  g_assert(filterx_builtin_function_ctor_register("keys", filterx_function_keys_new));
  g_assert(filterx_builtin_function_ctor_register("vars", filterx_function_vars_new));
  g_assert(filterx_builtin_function_ctor_register("get_timestamp", filterx_function_get_timestamp_new));
  g_assert(filterx_builtin_function_ctor_register("set_timestamp", filterx_function_set_timestamp_new));
  g_assert(filterx_builtin_function_ctor_register("set_pri", filterx_function_set_pri_new));
  g_assert(filterx_builtin_function_ctor_register("cache_json_file", filterx_function_cache_json_file_new));
  g_assert(filterx_builtin_function_ctor_register("regexp_search", filterx_function_regexp_search_new));
  g_assert(filterx_builtin_function_ctor_register("failure_info_enable", filterx_fn_failure_info_enable_new));
  g_assert(filterx_builtin_function_ctor_register("failure_info_clear", filterx_fn_failure_info_clear_new));
  g_assert(filterx_builtin_function_ctor_register("failure_info", filterx_fn_failure_info_new));
  g_assert(filterx_builtin_function_ctor_register("failure_info_meta", filterx_fn_failure_info_meta_new));
  g_assert(filterx_builtin_function_ctor_register("dict_to_pairs", filterx_function_dict_to_pairs_new));
}

static void
_ctors_deinit(void)
{
  filterx_builtin_function_ctors_deinit_private(filterx_builtin_function_ctors);
}

void
filterx_builtin_functions_init(void)
{
  _simple_init();
  _ctors_init();
}

void
filterx_builtin_functions_deinit(void)
{
  _simple_deinit();
  _ctors_deinit();
}

// FilterX types

FilterXType *
filterx_type_lookup(const gchar *type_name)
{
  return filterx_type_lookup_private(filterx_types, type_name);
}

gboolean
filterx_type_register(const gchar *type_name, FilterXType *fxtype)
{
  return filterx_type_register_private(filterx_types, type_name, fxtype);
}

void
filterx_types_init(void)
{
  filterx_types_init_private(&filterx_types);
  filterx_type_register("object", &FILTERX_TYPE_NAME(object));
}

void
filterx_types_deinit(void)
{
  filterx_types_deinit_private(filterx_types);
}

// Globals

void
filterx_global_init(void)
{
  filterx_types_init();

  filterx_type_init(&FILTERX_TYPE_NAME(list));
  filterx_type_init(&FILTERX_TYPE_NAME(dict));
  filterx_type_init(&FILTERX_TYPE_NAME(dict_object));
  filterx_type_init(&FILTERX_TYPE_NAME(list_object));

  filterx_type_init(&FILTERX_TYPE_NAME(null));
  filterx_type_init(&FILTERX_TYPE_NAME(integer));
  filterx_type_init(&FILTERX_TYPE_NAME(boolean));
  filterx_type_init(&FILTERX_TYPE_NAME(double));

  filterx_type_init(&FILTERX_TYPE_NAME(string));
  filterx_type_init(&FILTERX_TYPE_NAME(bytes));
  filterx_type_init(&FILTERX_TYPE_NAME(protobuf));

  filterx_type_init(&FILTERX_TYPE_NAME(datetime));
  filterx_type_init(&FILTERX_TYPE_NAME(message_value));

  filterx_type_init(&FILTERX_TYPE_NAME(metrics_labels));
  filterx_string_global_init();

  filterx_primitive_global_init();
  filterx_null_global_init();
  filterx_datetime_global_init();
  filterx_builtin_functions_init();
}

void
filterx_global_deinit(void)
{
  filterx_builtin_functions_deinit();
  filterx_datetime_global_deinit();
  filterx_null_global_deinit();
  filterx_primitive_global_deinit();
  filterx_string_global_deinit();
  filterx_types_deinit();
}

FilterXObject *
filterx_typecast_get_arg(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args == NULL || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument");
      return NULL;
    }

  return args[0];
}

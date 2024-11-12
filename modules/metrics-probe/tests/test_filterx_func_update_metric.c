/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include <criterion/criterion.h>
#include "metrics-probe-test.h"
#include "libtest/filterx-lib.h"

#include "filterx/func-update-metric.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "stats/stats-cluster-single.h"
#include "scratch-buffers.h"
#include "apphook.h"
#include "cfg.h"

static void
_add_label(FilterXExpr *labels_expr, const gchar *name, const gchar *value)
{
  FilterXObject *name_obj = filterx_string_new(name, -1);
  FilterXObject *value_obj = filterx_string_new(value, -1);
  FilterXObject *labels_obj = filterx_expr_eval(labels_expr);

  cr_assert(filterx_object_set_subscript(labels_obj, name_obj, &value_obj));

  filterx_object_unref(labels_obj);
  filterx_object_unref(value_obj);
  filterx_object_unref(name_obj);
}

static FilterXExpr *
_create_func(FilterXExpr *key, FilterXExpr *labels, FilterXExpr *increment, FilterXExpr *level)
{
  GList *args_list = NULL;

  cr_assert(key);
  args_list = g_list_append(args_list, filterx_function_arg_new(NULL, key));

  if (labels)
    args_list = g_list_append(args_list, filterx_function_arg_new("labels", labels));

  if (increment)
    args_list = g_list_append(args_list, filterx_function_arg_new("increment", increment));

  if (level)
    args_list = g_list_append(args_list, filterx_function_arg_new("level", level));

  GError *error = NULL;
  FilterXExpr *func = filterx_function_update_metric_new(filterx_function_args_new(args_list, &error), &error);
  cr_assert(!error, "Failed to create update_metric(): %s", error->message);
  cr_assert(func);

  return func;
}

static gboolean
_eval(FilterXExpr *func)
{
  FilterXObject *result = filterx_expr_eval(func);

  if (!result)
    return FALSE;

  gboolean result_bool;
  cr_assert(filterx_boolean_unwrap(result, &result_bool));
  cr_assert(result_bool);
  return TRUE;
}

Test(filterx_func_update_metric, key_and_labels)
{
  FilterXExpr *key = filterx_literal_new(filterx_string_new("test_key", -1));
  FilterXExpr *labels = filterx_non_literal_new(filterx_test_dict_new());
  _add_label(labels, "test_label_3", "baz");
  _add_label(labels, "test_label_1", "foo");
  _add_label(labels, "test_label_2", "bar");
  FilterXExpr *func = _create_func(key, labels, NULL, NULL);
  cr_assert(filterx_expr_init(func, configuration));

  StatsClusterLabel expected_labels[] =
  {
    stats_cluster_label("test_label_1", "foo"),
    stats_cluster_label("test_label_2", "bar"),
    stats_cluster_label("test_label_3", "baz"),
  };

  cr_assert(_eval(func));
  metrics_probe_test_assert_counter_value("test_key",
                                          expected_labels,
                                          G_N_ELEMENTS(expected_labels),
                                          1);

  cr_assert(_eval(func));
  metrics_probe_test_assert_counter_value("test_key",
                                          expected_labels,
                                          G_N_ELEMENTS(expected_labels),
                                          2);

  filterx_expr_deinit(func, configuration);
  filterx_expr_unref(func);
}

Test(filterx_func_update_metric, increment)
{
  FilterXExpr *key = filterx_literal_new(filterx_string_new("test_key", -1));
  FilterXExpr *func = _create_func(key, NULL, filterx_non_literal_new(filterx_integer_new(42)), NULL);
  cr_assert(filterx_expr_init(func, configuration));

  StatsClusterLabel expected_labels[] = {};

  cr_assert(_eval(func));
  metrics_probe_test_assert_counter_value("test_key",
                                          expected_labels,
                                          G_N_ELEMENTS(expected_labels),
                                          42);

  cr_assert(_eval(func));
  metrics_probe_test_assert_counter_value("test_key",
                                          expected_labels,
                                          G_N_ELEMENTS(expected_labels),
                                          84);

  filterx_expr_deinit(func, configuration);
  filterx_expr_unref(func);
}

Test(filterx_func_update_metric, level)
{
  FilterXExpr *func = NULL;
  StatsClusterLabel expected_labels[] = {};

  configuration->stats_options.level = STATS_LEVEL0;
  cr_assert(cfg_init(configuration));
  func = _create_func(filterx_literal_new(filterx_string_new("test_key", -1)), NULL, NULL,
                      filterx_literal_new(filterx_integer_new(2)));
  cr_assert(filterx_expr_init(func, configuration));
  cr_assert(_eval(func));
  filterx_expr_deinit(func, configuration);
  filterx_expr_unref(func);
  cr_assert_not(metrics_probe_test_stats_cluster_exists("test_key", expected_labels, G_N_ELEMENTS(expected_labels)));
  cr_assert(cfg_deinit(configuration));

  configuration->stats_options.level = STATS_LEVEL1;
  cr_assert(cfg_init(configuration));
  func = _create_func(filterx_literal_new(filterx_string_new("test_key", -1)), NULL, NULL,
                      filterx_literal_new(filterx_integer_new(2)));
  cr_assert(filterx_expr_init(func, configuration));
  cr_assert(_eval(func));
  filterx_expr_deinit(func, configuration);
  filterx_expr_unref(func);
  cr_assert_not(metrics_probe_test_stats_cluster_exists("test_key", expected_labels, G_N_ELEMENTS(expected_labels)));
  cr_assert(cfg_deinit(configuration));

  configuration->stats_options.level = STATS_LEVEL2;
  cr_assert(cfg_init(configuration));
  func = _create_func(filterx_literal_new(filterx_string_new("test_key", -1)), NULL, NULL,
                      filterx_literal_new(filterx_integer_new(2)));
  cr_assert(filterx_expr_init(func, configuration));
  cr_assert(_eval(func));
  filterx_expr_deinit(func, configuration);
  filterx_expr_unref(func);
  metrics_probe_test_assert_counter_value("test_key",
                                          expected_labels,
                                          G_N_ELEMENTS(expected_labels),
                                          1);
  cr_assert(cfg_deinit(configuration));
}

void setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
  init_libtest_filterx();
}

void teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_func_update_metric, .init = setup, .fini = teardown);

/*
 * Copyright (c) 2017 Balabit
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

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include "apphook.h"
#include "logmsg/logmsg.h"
#include "stats/stats-cluster.h"
#include "stats/stats-cluster-single.h"
#include "stats/stats-counter.h"
#include "stats/stats-query.h"
#include "stats/stats-registry.h"
#include <string.h>

guint SCS_FILE;
guint SCS_PIPE;
guint SCS_TCP;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef struct _CounterHashContent
{
  const gint component;
  const gchar *id;
  const gchar *instance;
  const gint type;
  atomic_gssize *external_counter;
  gssize initial_value;
} CounterHashContent;

typedef struct _QueryTestCase
{
  const gchar *pattern;
  const gchar *expected;
} QueryTestCase;

typedef void(*ClusterKeySet)(StatsClusterKey *, guint16, const gchar *, const gchar *);

static void
setup(void)
{
  app_startup();
  SCS_FILE = stats_register_type("file");
  SCS_PIPE = stats_register_type("pipe");
  SCS_TCP = stats_register_type("tcp");
}

static void
_register_counters(const CounterHashContent *counters, size_t n, ClusterKeySet key_set)
{
  stats_lock();
  for (size_t i = 0; i < n; i++)
    {
      StatsClusterKey sc_key;
      key_set(&sc_key, counters[i].component, counters[i].id, counters[i].instance);
      if (counters[i].external_counter)
        {
          atomic_gssize_set(counters[i].external_counter, counters[i].initial_value);
          stats_register_external_counter(0, &sc_key, counters[i].type, counters[i].external_counter);
        }
      else
        {
          StatsCounterItem *item = NULL;
          stats_register_counter(0, &sc_key, counters[i].type, &item);
        }
    }
  stats_unlock();
}

static void
_register_single_counter_with_name(void)
{
  stats_lock();
  {
    StatsClusterKey sc_key;
    StatsCounterItem *ctr_item;
    stats_cluster_single_key_legacy_set_with_name(&sc_key, SCS_GLOBAL, "id", "instance", "name");
    stats_register_counter(0, &sc_key, SC_TYPE_SINGLE_VALUE, &ctr_item);
  }
  stats_unlock();
}

static void
_initialize_counter_hash(void)
{
  setup();
  static atomic_gssize external_frozen_ctr;

  const CounterHashContent logpipe_cluster_counters[] =
  {
    {SCS_CENTER, "guba.polo", "frozen", SC_TYPE_SUPPRESSED, &external_frozen_ctr, 12},
    {SCS_FILE | SCS_SOURCE, "guba", "processed", SC_TYPE_PROCESSED, NULL, 0},
    {SCS_GLOBAL, "guba.gumi.diszno", "frozen", SC_TYPE_SUPPRESSED, NULL, 0},
    {SCS_PIPE | SCS_SOURCE, "guba.gumi.disz", "frozen", SC_TYPE_SUPPRESSED, NULL, 0},
    {SCS_TCP | SCS_DESTINATION, "guba.labda", "received", SC_TYPE_DROPPED, NULL, 0},
    {SCS_TCP | SCS_SOURCE, "guba.frizbi", "left", SC_TYPE_QUEUED, NULL, 0},
  };

  const CounterHashContent single_cluster_counters[] =
  {
    {SCS_GLOBAL, "", "guba", SC_TYPE_SINGLE_VALUE, NULL, 0}
  };

  _register_counters(logpipe_cluster_counters, ARRAY_SIZE(logpipe_cluster_counters),
                     stats_cluster_logpipe_key_legacy_set);
  _register_counters(single_cluster_counters, ARRAY_SIZE(single_cluster_counters), stats_cluster_single_key_legacy_set);
  _register_single_counter_with_name();
}

static gboolean
_test_format_log_msg_get(gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  StatsCounterItem *ctr = (StatsCounterItem *) args[0];
  LogMessage *msg = (LogMessage *) args[1];
  gchar *name, *value;

  name = g_strdup_printf("%s", stats_counter_get_name(ctr));
  value = g_strdup_printf("%"G_GSIZE_FORMAT, stats_counter_get(ctr));

  log_msg_set_value_by_name(msg, name, value, -1);

  g_free(name);
  g_free(value);

  return TRUE;
}

static gboolean
_test_format_str_get(gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  StatsCounterItem *ctr = (StatsCounterItem *) args[0];
  GString *str = (GString *) args[1];
  g_string_append_printf(str, "%s: %"G_GSIZE_FORMAT"\n", stats_counter_get_name(ctr), stats_counter_get(ctr));

  return TRUE;
}

static gboolean
_test_format_log_msg_get_sum(gpointer user_data)
{
  gchar *name, *value;
  gpointer *args = (gpointer *) user_data;
  LogMessage *msg = (LogMessage *) args[0];
  gint *sum = (gint *) args[1];

  name = "sum";
  value = g_strdup_printf("%d", *sum);

  log_msg_set_value_by_name(msg, name, value, -1);

  g_free(value);

  return TRUE;
}

static gboolean
_test_format_str_get_sum(gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  GString *result = (GString *) args[0];
  gint *sum = (gint *) args[1];

  g_string_printf(result, "%d", *sum);

  return TRUE;
}

static gboolean
_test_format_list(gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  StatsCounterItem *ctr = (StatsCounterItem *) args[0];
  GString *str = (GString *) args[1];
  g_string_append_printf(str, "%s\n", stats_counter_get_name(ctr));

  return TRUE;
}

TestSuite(cluster_query_key, .init = setup, .fini = app_shutdown);

Test(cluster_query_key, test_global_key)
{
  const gchar *expected_key = "dst.file.d_file.instance";
  StatsClusterKey sc_key;
  stats_cluster_logpipe_key_legacy_set(&sc_key, SCS_DESTINATION|SCS_FILE, "d_file", "instance" );
  StatsCluster *sc = stats_cluster_new(&sc_key);
  cr_assert_str_eq(sc->query_key, expected_key,
                   "generated query key(%s) does not match to the expected key(%s)",
                   sc->query_key, expected_key);
  stats_cluster_free(sc);
}

TestSuite(stats_query, .init = _initialize_counter_hash, .fini = app_shutdown);

ParameterizedTestParameters(stats_query, test_stats_query_get_log_msg_out)
{
  static QueryTestCase test_cases[] =
  {
    {"dst.tcp.guba.labda.received.dropped", "0"},
    {"src.pipe.guba.gumi.disz.frozen.suppressed", "0"},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_get_log_msg_out)
{
  const gchar *actual;
  LogMessage *msg = log_msg_new_empty();

  stats_query_get(test_cases->pattern, _test_format_log_msg_get, (gpointer)msg);
  actual = log_msg_get_value_by_name(msg, test_cases->pattern, NULL);
  cr_assert_str_eq(actual, test_cases->expected,
                   "Counter: '%s'; expected number: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, actual);

  log_msg_unref(msg);
}


ParameterizedTestParameters(stats_query, test_stats_query_get_str_out)
{
  static QueryTestCase test_cases[] =
  {
    {"center.*.*", "center.guba.polo.frozen.suppressed: 12\n"},
    {"cent*", "center.guba.polo.frozen.suppressed: 12\n"},
    {"src.pipe.guba.gumi.disz.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"},
    {"src.pipe.guba.gumi.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"},
    {"src.pipe.guba.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"},
    {"src.pipe.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed: 0\n"},
    {"dst.*.*", "dst.tcp.guba.labda.received.dropped: 0\n"},
    {"dst.*.*.*", "dst.tcp.guba.labda.received.dropped: 0\n"},
    {"dst.*.*.*.*", "dst.tcp.guba.labda.received.dropped: 0\n"},
    {"src.java.*.*", ""},
    {"src.ja*.*.*", ""},
    {"global.id.instance.name", "global.id.instance.name: 0\n"},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_get_str_out)
{
  GString *result = g_string_new("");

  stats_query_get(test_cases->pattern, _test_format_str_get, (gpointer)result);
  cr_assert_str_eq(result->str, test_cases->expected,
                   "Pattern: '%s'; expected key and value: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, result->str);

  g_string_free(result, TRUE);
}

ParameterizedTestParameters(stats_query, test_stats_query_get_sum_log_msg_out)
{
  static QueryTestCase test_cases[] =
  {
    {"dst.tcp.guba.labda.received.dropped", "0"},
    {"src.pipe.guba.gumi.disz.frozen.suppressed", "0"},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_get_sum_log_msg_out)
{
  const gchar *actual;
  LogMessage *msg = log_msg_new_empty();

  stats_query_get_sum(test_cases->pattern, _test_format_log_msg_get_sum, (gpointer)msg);
  actual = log_msg_get_value_by_name(msg, "sum", NULL);
  cr_assert_str_eq(actual, test_cases->expected,
                   "Pattern: '%s'; expected number: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, actual);

  log_msg_unref(msg);
}

ParameterizedTestParameters(stats_query, test_stats_query_get_sum_str_out)
{
  static QueryTestCase test_cases[] =
  {
    {"*", "12"},
    {"center.*.*", "12"},
    {"cent*", "12"},
    {"src.pipe.guba.gumi.disz.*.*", "0"},
    {"*.tcp.guba.*.*", "0"},
    {"*.guba.*i.*.*", "0"},
    {"*.guba.gum?.*.*", "0"},
    {"src.ja*.*.*", ""},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_get_sum_str_out)
{
  GString *result = g_string_new("");

  stats_query_get_sum(test_cases->pattern, _test_format_str_get_sum, (gpointer)result);
  cr_assert_str_eq(result->str, test_cases->expected,
                   "Pattern: '%s'; expected key and value: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, result->str);

  g_string_free(result, TRUE);
}

ParameterizedTestParameters(stats_query, test_stats_query_list)
{
  static QueryTestCase test_cases[] =
  {
    {"center.*.*", "center.guba.polo.frozen.suppressed\n"},
    {"cent*", "center.guba.polo.frozen.suppressed\n"},
    {"src.pipe.guba.gumi.disz.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed\n"},
    {"src.pipe.*.*", "src.pipe.guba.gumi.disz.frozen.suppressed\n"},
    {"src.java.*.*", ""},
    {"src.ja*.*.*", ""},
  };

  return cr_make_param_array(QueryTestCase, test_cases, sizeof(test_cases) / sizeof(test_cases[0]));
}

ParameterizedTest(QueryTestCase *test_cases, stats_query, test_stats_query_list)
{
  GString *result = g_string_new("");

  stats_query_list(test_cases->pattern, _test_format_list, (gpointer)result);
  cr_assert_str_eq(result->str, test_cases->expected,
                   "Pattern: '%s'; expected key: '%s';, got: '%s';", test_cases->pattern, test_cases->expected, result->str);

  g_string_free(result, TRUE);
}

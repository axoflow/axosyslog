/*
 * Copyright (c) 2026 One Identity
 * Copyright (c) 2026 Hofi <hofione@gmail.com>
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

#include "libtest/grab-logging.h"

#include "kafka-source-driver.h"
#include "kafka-source-persist.h"
#include "kafka-props.h"
#include "apphook.h"

/*
 * Criterion parameter payloads must be self-contained here.
 * We use fixed-size arrays (not pointers) to avoid pointer invalidation across
 * worker process boundaries on macOS
 */
struct option_string_test_params
{
  gchar value[64];
};

struct topic_partition_test_params
{
  gchar topic[256];
  gchar partitions[64];
};

static void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
  start_grabbing_messages();
}

static void
teardown(void)
{
  stop_grabbing_messages();
  cfg_free(configuration);
  app_shutdown();
}

TestSuite(kafka_source, .init = setup, .fini = teardown);

/* ******************
 *  strategy-hint()
 * ******************/

ParameterizedTestParameters(kafka_source, valid_strategy_hint_tests)
{
  static struct option_string_test_params params[] =
  {
    {"subscribe"},
    {"assign"},
  };

  return cr_make_param_array(struct option_string_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct option_string_test_params *param, kafka_source, valid_strategy_hint_tests)
{
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_eq(kafka_sd_set_strategy_hint(driver, param->value), TRUE,
               "expected `%s` to be a valid strategy-hint", param->value);
  log_pipe_unref(&driver->super);
}

ParameterizedTestParameters(kafka_source, invalid_strategy_hint_tests)
{
  static struct option_string_test_params params[] =
  {
    {""},
    {"foo"},
    {"SUBSCRIBE"},
    {"Assign"},
  };

  return cr_make_param_array(struct option_string_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct option_string_test_params *param, kafka_source, invalid_strategy_hint_tests)
{
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_eq(kafka_sd_set_strategy_hint(driver, param->value), FALSE,
               "expected `%s` to be an invalid strategy-hint", param->value);
  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_strategy_hint_null_is_rejected)
{
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_not(kafka_sd_set_strategy_hint(driver, NULL));
  log_pipe_unref(&driver->super);
}

/* ****************
 *  persist-store()
 * ****************/

ParameterizedTestParameters(kafka_source, valid_persist_store_tests)
{
  static struct option_string_test_params params[] =
  {
    {"local"},
    {"remote"},
  };

  return cr_make_param_array(struct option_string_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct option_string_test_params *param, kafka_source, valid_persist_store_tests)
{
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_eq(kafka_sd_set_persis_store(driver, param->value), TRUE,
               "expected `%s` to be a valid persist-store", param->value);
  log_pipe_unref(&driver->super);
}

ParameterizedTestParameters(kafka_source, invalid_persist_store_tests)
{
  static struct option_string_test_params params[] =
  {
    {""},
    {"foo"},
    {"LOCAL"},
    {"Remote"},
  };

  return cr_make_param_array(struct option_string_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct option_string_test_params *param, kafka_source, invalid_persist_store_tests)
{
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_eq(kafka_sd_set_persis_store(driver, param->value), FALSE,
               "expected `%s` to be an invalid persist-store", param->value);
  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_persis_store_null_is_rejected)
{
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_not(kafka_sd_set_persis_store(driver, NULL));
  log_pipe_unref(&driver->super);
}

/* ************
 *  logging()
 * ************
 *
 * kafka_sd_set_logging() matches values case-insensitively
 * (via g_ascii_strcasecmp in kafka_string_to_logging).
 */

ParameterizedTestParameters(kafka_source, valid_logging_tests)
{
  static struct option_string_test_params params[] =
  {
    {"disabled"},
    {"kafka"},
    {"trace"},
    {"Disabled"},
    {"KAFKA"},
    {"TrAcE"},
  };

  return cr_make_param_array(struct option_string_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct option_string_test_params *param, kafka_source, valid_logging_tests)
{
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_eq(kafka_sd_set_logging(driver, param->value), TRUE,
               "expected `%s` to be a valid logging value", param->value);
  log_pipe_unref(&driver->super);
}

ParameterizedTestParameters(kafka_source, invalid_logging_tests)
{
  static struct option_string_test_params params[] =
  {
    {""},
    {"foo"},
    {"info"},
    {"debug"},
  };

  return cr_make_param_array(struct option_string_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct option_string_test_params *param, kafka_source, invalid_logging_tests)
{
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_eq(kafka_sd_set_logging(driver, param->value), FALSE,
               "expected `%s` to be an invalid logging value", param->value);
  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_logging_null_is_rejected)
{
  /* Pins the kafka_string_to_logging() NULL-guard: before that fix
   * g_ascii_strcasecmp(NULL, ...) would crash. Symmetric with
   * set_strategy_hint_null_is_rejected / set_persis_store_null_is_rejected. */
  LogDriver *driver = kafka_sd_new(configuration);
  cr_assert_not(kafka_sd_set_logging(driver, NULL));
  log_pipe_unref(&driver->super);
}

/* **************************
 *  state-update-timeout()
 * **************************/

Test(kafka_source, state_update_timeout_below_minimum_is_raised_with_a_warning)
{
  LogDriver *driver = kafka_sd_new(configuration);
  KafkaSourceDriver *self = (KafkaSourceDriver *) driver;

  kafka_sd_set_state_update_timeout(driver, KAFKA_STATE_UPDATE_TIMEOUT_MIN - 1);
  cr_assert_eq(self->options.state_update_timeout, KAFKA_STATE_UPDATE_TIMEOUT_MIN);
  assert_grabbed_log_contains("kafka: state-update-timeout() is below the minimum");

  kafka_sd_set_state_update_timeout(driver, KAFKA_STATE_UPDATE_TIMEOUT_MIN);
  cr_assert_eq(self->options.state_update_timeout, KAFKA_STATE_UPDATE_TIMEOUT_MIN);
  kafka_sd_set_state_update_timeout(driver, 2 * KAFKA_STATE_UPDATE_TIMEOUT_MIN);
  cr_assert_eq(self->options.state_update_timeout, 2 * KAFKA_STATE_UPDATE_TIMEOUT_MIN);

  log_pipe_unref(&driver->super);
}

/* ************************
 *  Persist entry name
 * ************************
 *
 * kafka_source_persist_format_name() builds the persist entry name, which is
 * also the hash key the driver finds the KafkaSourcePersist instances by (see
 * _find_persist() / _partitions_persists_create() in kafka-source-driver.c).
 * Any change to the "<topic>#<partition>" format would silently break offset
 * recovery from on-disk state files for upgraded users.
 */

struct persist_name_test_params
{
  gchar topic[256];
  int32_t partition;
  gchar expected[MAX_KAFKA_PARTITION_KEY_NAME_LEN];
};

ParameterizedTestParameters(kafka_source, persist_format_name_tests)
{
  static struct persist_name_test_params params[] =
  {
    {"plain",                0,          "plain#0"},
    {"plain",                -1,         "plain#-1"},
    {"topic.a",              42,         "topic.a#42"},
    {"with-dashes_and.dots", 2147483647, "with-dashes_and.dots#2147483647"},
  };

  return cr_make_param_array(struct persist_name_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct persist_name_test_params *param, kafka_source, persist_format_name_tests)
{
  gchar buf[MAX_KAFKA_PARTITION_KEY_NAME_LEN];
  gchar *result = kafka_source_persist_format_name(param->topic, param->partition, buf, sizeof(buf));

  cr_assert_eq(result, buf, "expected return value to point to the destination buffer");
  cr_assert_str_eq(buf, param->expected,
                   "expected key for topic=`%s` partition=%d to be `%s`, got `%s`",
                   param->topic, param->partition, param->expected, buf);
}

Test(kafka_source, persist_format_name_truncates_into_buffer)
{
  gchar fits[8]; /* exactly fits "abc#42\0" */
  kafka_source_persist_format_name("abc", 42, fits, sizeof(fits));
  cr_assert_str_eq(fits, "abc#42");

  /* g_snprintf truncates the formatted output to fit the buffer (left-anchored,
   * right-truncated) and always null-terminates. Pin both properties — a silent
   * change to either would corrupt persist entry names for upgraded users. */
  gchar small[4]; /* forces truncation: "abc\0" */
  kafka_source_persist_format_name("abc", 42, small, sizeof(small));
  cr_assert_eq(strlen(small), sizeof(small) - 1,
               "g_snprintf must truncate and null-terminate within the buffer");
  cr_assert_str_eq(small, "abc",
                   "truncation must be right-side: keep the prefix, drop the tail");
}

/* ***********
 *  topic()
 * ***********
 *
 * topic() takes a list of KafkaProperty entries where name=topic and
 * value=comma-separated partition numbers (or "-1" for all partitions).
 * Ownership of the list passes to the driver on every call regardless of
 * the return value, so the cleanup happens via kafka_sd_options_destroy().
 */

static GList *
_topic_list_append(GList *list, const gchar *topic, const gchar *partitions)
{
  return g_list_append(list, kafka_property_new(topic, partitions));
}

ParameterizedTestParameters(kafka_source, set_topics_valid_tests)
{
  static struct topic_partition_test_params params[] =
  {
    {"plain-topic", "0"},
    {"plain-topic", "0,1,2"},
    {"plain-topic", "-1"},
    /* '^' marks a regex pattern, as rd_kafka_subscribe() defines it */
    {"^prefix_[0-9]+$", "-1"},
    {"^topic.*", "-1"},
    {"^(foo|bar)", "0"},
    /* normalization: whitespace around partition tokens is stripped */
    {"plain-topic", "0, 1 , 2"},
    /* normalization: partitions are sorted internally */
    {"plain-topic", "2,1,0"},
    /* normalization: duplicate partition numbers are silently dropped */
    {"plain-topic", "0,1,1,2"},
  };

  return cr_make_param_array(struct topic_partition_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct topic_partition_test_params *param, kafka_source, set_topics_valid_tests)
{
  LogDriver *driver = kafka_sd_new(configuration);
  GList *topics = _topic_list_append(NULL, param->topic, param->partitions);

  cr_assert_eq(kafka_sd_set_topics(driver, topics), TRUE,
               "expected topic=`%s` partitions=`%s` to be accepted",
               param->topic, param->partitions);

  log_pipe_unref(&driver->super);
}

ParameterizedTestParameters(kafka_source, set_topics_invalid_tests)
{
  static struct topic_partition_test_params params[] =
  {
    /* malformed partition spec */
    {"plain-topic", "foo"},
    {"plain-topic", "0,foo"},
    {"plain-topic", ""},
    /* whitespace-only partition spec (all-empty after strip) */
    {"plain-topic", "   "},
    /* comma-only partition spec (each token empty after strip) */
    {"plain-topic", ", ,"},
    /* "all" cannot be mixed with specific partitions */
    {"plain-topic", "-1,0"},
    /* partition number below RD_KAFKA_PARTITION_UA (-1) */
    {"plain-topic", "-2"},
    /* without the '^' prefix a topic is a literal name, regex syntax is illegal in it */
    {"topic.*", "0"},
    {"foo|bar", "0"},
    {"[bad", "0"},
    {"(bad", "0"},
    /* '^' patterns must compile as extended regular expressions */
    {"^[bad", "0"},
    {"^(bad", "0"},
    {"^a{2", "0"},
    /* empty topic name */
    {"", "0"},
  };

  return cr_make_param_array(struct topic_partition_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct topic_partition_test_params *param, kafka_source, set_topics_invalid_tests)
{
  LogDriver *driver = kafka_sd_new(configuration);
  GList *topics = _topic_list_append(NULL, param->topic, param->partitions);

  cr_assert_eq(kafka_sd_set_topics(driver, topics), FALSE,
               "expected topic=`%s` partitions=`%s` to be rejected",
               param->topic, param->partitions);

  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_topics_empty_list_is_rejected)
{
  LogDriver *driver = kafka_sd_new(configuration);

  cr_assert_not(kafka_sd_set_topics(driver, NULL));

  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_topics_recall_replaces_previous_list)
{
  LogDriver *driver = kafka_sd_new(configuration);

  cr_assert(kafka_sd_set_topics(driver, _topic_list_append(NULL, "first-topic", "0")));

  /* Re-call with a different valid list: previous list must be freed cleanly. */
  cr_assert(kafka_sd_set_topics(driver, _topic_list_append(NULL, "second-topic", "1,2")));

  /* Re-call with an invalid list still returns FALSE even when valid topics
   * are already set; the driver replaces the stored list regardless. */
  cr_assert_not(kafka_sd_set_topics(driver, _topic_list_append(NULL, "third-topic", "foo")));

  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_topics_mixed_valid_invalid_rejected)
{
  /* The first (valid) entry is processed and prepended to the local
   * requested_topics list; the second entry's failure must trigger the
   * partial-list cleanup path in _check_and_apply_topics without leaking. */
  LogDriver *driver = kafka_sd_new(configuration);
  GList *topics = _topic_list_append(NULL, "valid-topic", "0");
  topics = _topic_list_append(topics, "bad-spec", "foo");

  cr_assert_not(kafka_sd_set_topics(driver, topics));

  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_topics_recovers_after_failed_call)
{
  /* A failed set_topics() still stores the rejected list internally; a
   * subsequent valid set_topics() must replace it cleanly and succeed. */
  LogDriver *driver = kafka_sd_new(configuration);

  cr_assert_not(kafka_sd_set_topics(driver, _topic_list_append(NULL, "bad-spec", "foo")));
  cr_assert(kafka_sd_set_topics(driver, _topic_list_append(NULL, "good-topic", "0")));

  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_topics_with_duplicate_entries_is_accepted)
{
  /* Duplicates are silently deduplicated at apply time (init), not by the
   * setter. set_topics() must accept the list as-is. */
  LogDriver *driver = kafka_sd_new(configuration);
  GList *topics = _topic_list_append(NULL, "dup-topic", "0");
  topics = _topic_list_append(topics, "dup-topic", "0");

  cr_assert(kafka_sd_set_topics(driver, topics));

  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_topics_empty_topic_name_emits_error_log)
{
  LogDriver *driver = kafka_sd_new(configuration);
  GList *topics = _topic_list_append(NULL, "", "0");

  cr_assert_not(kafka_sd_set_topics(driver, topics));
  assert_grabbed_log_contains("kafka: invalid topic name in the requested topics list");
  /* the grabbed log escapes the apostrophe in "can't", so stop before it */
  assert_grabbed_log_contains("topic name is illegal, it can");

  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_topics_bad_pattern_emits_error_log_with_the_regex_error)
{
  LogDriver *driver = kafka_sd_new(configuration);
  GList *topics = _topic_list_append(NULL, "^[bad", "0");

  cr_assert_not(kafka_sd_set_topics(driver, topics));
  assert_grabbed_log_contains("kafka: invalid topic name in the requested topics list");
  assert_grabbed_log_contains("badly formatted regex pattern: ");

  log_pipe_unref(&driver->super);
}

Test(kafka_source, set_topics_unanchored_regex_emits_error_log_with_a_hint)
{
  /* "topic.*" compiles as a regex but rd_kafka_subscribe() would take it as
   * a literal name, so it is rejected as a name, pointing at the '^' prefix */
  LogDriver *driver = kafka_sd_new(configuration);
  GList *topics = _topic_list_append(NULL, "topic.*", "0");

  cr_assert_not(kafka_sd_set_topics(driver, topics));
  assert_grabbed_log_contains("kafka: invalid topic name in the requested topics list");
  /* the grabbed log escapes the quotes around ^, so stop before them */
  assert_grabbed_log_contains("characters other than pattern [-._a-zA-Z0-9]+, a regex pattern must start with ");

  log_pipe_unref(&driver->super);
}

/* ***************************
 *  Mandatory option handling
 * ***************************/

static void
_set_one_topic(LogDriver *driver, const gchar *topic, const gchar *partitions)
{
  GList *topics = _topic_list_append(NULL, topic, partitions);
  cr_assert(kafka_sd_set_topics(driver, topics));
}

Test(kafka_source, test_topic_is_mandatory)
{
  LogDriver *driver = kafka_sd_new(configuration);
  kafka_sd_set_bootstrap_servers(driver, "localhost:9092");

  cr_assert_not(log_pipe_init(&driver->super));
  assert_grabbed_log_contains("kafka: the topic() argument is required for kafka source");

  log_pipe_deinit(&driver->super);
  log_pipe_unref(&driver->super);
}

Test(kafka_source, test_bootstrap_server_is_mandatory)
{
  LogDriver *driver = kafka_sd_new(configuration);
  _set_one_topic(driver, "default-test-topic", "0");

  cr_assert_not(log_pipe_init(&driver->super));
  assert_grabbed_log_contains("kafka: the bootstrap-servers() option is required for kafka source");

  log_pipe_deinit(&driver->super);
  log_pipe_unref(&driver->super);
}

/*
 * NOTE: A "happy path" init test (topic + bootstrap-servers set) is intentionally
 * omitted here. Unlike LogThreadedDestDriver, a successful LogThreadedSourceDriver
 * init constructs the librdkafka client and its background threads, which require
 * a fully initialised main loop to tear down cleanly during deinit
 * (see lib/logthrsource/tests/test_logthrsourcedrv.c for the full pattern).
 * That belongs in an integration/light test, not a unit test.
 */

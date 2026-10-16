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

#include "kafka-internal.h"
#include "messages.h"
#include "logmsg/logmsg.h"

static void
setup(void)
{
  msg_init(FALSE);
  log_tags_global_init();
  log_msg_global_init();
}

static void
teardown(void)
{
  msg_deinit();
}

TestSuite(kafka_internal, .init = setup, .fini = teardown);

/* *********************
 *  Pattern validation
 * *********************
 *
 * The source driver validates topics starting with '^', which
 * rd_kafka_subscribe() takes as regex patterns, with
 * kafka_validate_topic_pattern(): the pattern must compile as a POSIX
 * extended regular expression, the engine librdkafka itself uses. Everything
 * else is a strict topic name (see test_kafka_topic.c). The function only
 * checks compilability, the '^' prefix is the driver's concern.
 */

/* Criterion parameter payloads must be self-contained — fixed-size arrays. */
struct topic_pattern_test_params
{
  gchar pattern[256];
};

ParameterizedTestParameters(kafka_internal, valid_topic_pattern_tests)
{
  static struct topic_pattern_test_params params[] =
  {
    {"validname123"},
    {"valid.name-123"},
    {"topic.*"},
    {"^prefix_[0-9]+$"},
    {"foo|bar"},
    {"top[0-9]+"},
    {".+"},
  };

  return cr_make_param_array(struct topic_pattern_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct topic_pattern_test_params *param, kafka_internal, valid_topic_pattern_tests)
{
  GError *error = NULL;
  cr_assert_eq(kafka_validate_topic_pattern(param->pattern, &error), TRUE,
               "expected `%s` to be a valid pattern", param->pattern);
  cr_assert_null(error);
}

ParameterizedTestParameters(kafka_internal, invalid_topic_pattern_tests)
{
  static struct topic_pattern_test_params params[] =
  {
    {""},
    {"["},
    {"("},
    {"a[b"},
  };

  return cr_make_param_array(struct topic_pattern_test_params, params, G_N_ELEMENTS(params));
}

ParameterizedTest(struct topic_pattern_test_params *param, kafka_internal, invalid_topic_pattern_tests)
{
  GError *error = NULL;
  cr_assert_eq(kafka_validate_topic_pattern(param->pattern, &error), FALSE,
               "expected `%s` to be an invalid pattern", param->pattern);
  if (error)
    g_error_free(error);
}

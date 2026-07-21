/*
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "openobserve-adapter.h"
#include "http-adapter.h"
#include "apphook.h"
#include "modules/http/http-signals.h"
#include <criterion/criterion.h>

static inline gchar *
_extract_offending_request(HttpResponseSignalData *data)
{
  if (data->offending_request_len != 0)
    return g_strndup(&data->request_body->str[data->offending_request_start], data->offending_request_len);
  else
    return g_strdup(data->request_body->str);
}

Test(openobserve_adapter, test_partial_failure_sets_offending_message)
{
  HttpAdapter *oa = openobserve_adapter_new();
  HttpResponseSignalData data =
  {
    .http_code = 200,
    .batch_size = 4,
    .request_body = g_string_new("msg1\nmsg2\nmsg3\nmsg4\n"),
    .response_body = g_string_new(
      "{\"code\":200,\"status\":[{\"name\":\"network\",\"successful\":2,\"failed\":2,\"error\":\"Cant parse timestamp\"}]}"
    ),
    0
  };

  http_adapter_adapt_response(oa, &data);

  cr_assert_eq(data.result, HTTP_SLOT_CRITICAL_ERROR);
  cr_assert_eq(data.offending_message, 2);

  gchar *offending_request = _extract_offending_request(&data);
  cr_assert_str_eq(offending_request, "msg3");
  g_free(offending_request);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(oa);
}

Test(openobserve_adapter, test_all_failed_points_to_first_message)
{
  HttpAdapter *oa = openobserve_adapter_new();
  HttpResponseSignalData data =
  {
    .http_code = 200,
    .batch_size = 3,
    .request_body = g_string_new("msg1\nmsg2\nmsg3\n"),
    .response_body = g_string_new(
      "{\"code\":200,\"status\":[{\"name\":\"network\",\"successful\":0,\"failed\":3,\"error\":\"Cant parse timestamp\"}]}"
    ),
    0
  };

  http_adapter_adapt_response(oa, &data);

  cr_assert_eq(data.result, HTTP_SLOT_CRITICAL_ERROR);
  cr_assert_eq(data.offending_message, 0);

  gchar *offending_request = _extract_offending_request(&data);
  cr_assert_str_eq(offending_request, "msg1");
  g_free(offending_request);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(oa);
}

Test(openobserve_adapter, test_all_successful_no_change)
{
  HttpAdapter *oa = openobserve_adapter_new();
  HttpResponseSignalData data =
  {
    .http_code = 200,
    .batch_size = 3,
    .request_body = g_string_new("msg1\nmsg2\nmsg3\n"),
    .response_body = g_string_new(
      "{\"code\":200,\"status\":[{\"name\":\"network\",\"successful\":3,\"failed\":0}]}"
    ),
    .offending_message = 0,
    .offending_request_start = 0,
    .offending_request_len = 0,
  };

  http_adapter_adapt_response(oa, &data);

  cr_assert_eq(data.http_code, 200);
  cr_assert_eq(data.offending_message, 0);
  cr_assert_eq(data.offending_request_start, 0);
  cr_assert_eq(data.offending_request_len, 0);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(oa);
}

Test(openobserve_adapter, test_multiple_status_entries_sum_successful)
{
  HttpAdapter *oa = openobserve_adapter_new();
  /* Two streams: first has 2 successful, second has 1 successful then 2 failed */
  HttpResponseSignalData data =
  {
    .http_code = 200,
    .batch_size = 5,
    .request_body = g_string_new("msg1\nmsg2\nmsg3\nmsg4\nmsg5\n"),
    .response_body = g_string_new(
      "{\"code\":200,\"status\":["
      "{\"name\":\"stream_a\",\"successful\":2,\"failed\":0},"
      "{\"name\":\"stream_b\",\"successful\":1,\"failed\":2,\"error\":\"invalid field\"}"
      "]}"
    ),
    0
  };

  http_adapter_adapt_response(oa, &data);

  cr_assert_eq(data.result, HTTP_SLOT_CRITICAL_ERROR);
  /* First failed message is at index 2+1=3 (0-based) */
  cr_assert_eq(data.offending_message, 3);

  gchar *offending_request = _extract_offending_request(&data);
  cr_assert_str_eq(offending_request, "msg4");
  g_free(offending_request);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(oa);
}

Test(openobserve_adapter, test_successful_count_exceeds_batch_size_resets_to_zero)
{
  HttpAdapter *oa = openobserve_adapter_new();
  /* successful count is larger than batch_size (shouldn't happen in practice, but be safe) */
  HttpResponseSignalData data =
  {
    .http_code = 200,
    .batch_size = 3,
    .request_body = g_string_new("msg1\nmsg2\nmsg3\n"),
    .response_body = g_string_new(
      "{\"code\":200,\"status\":[{\"name\":\"network\",\"successful\":5,\"failed\":2,\"error\":\"overflow\"}]}"
    ),
    0
  };

  http_adapter_adapt_response(oa, &data);

  cr_assert_eq(data.result, HTTP_SLOT_CRITICAL_ERROR);
  cr_assert_eq(data.offending_message, 0);
  cr_assert_eq(data.offending_request_start, 0);
  cr_assert_eq(data.offending_request_len, 0);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(oa);
}

Test(openobserve_adapter, test_successful_request_action)
{
  HttpAdapter *oa = openobserve_adapter_new();
  /* successful count is larger than batch_size (shouldn't happen in practice, but be safe) */
  HttpResponseSignalData data =
  {
    .http_code = 200,
    .batch_size = 3,
    .request_body = g_string_new("msg1\nmsg2\nmsg3\n"),
    .response_body = g_string_new(
      "{\"code\":200,\"status\":[{\"name\":\"default\",\"successful\":1,\"failed\":0}]}"
    ),
    0
  };

  http_adapter_adapt_response(oa, &data);

  cr_assert_eq(data.http_code, 200);
  cr_assert_eq(data.offending_message, 0);
  cr_assert_eq(data.offending_request_start, 0);
  cr_assert_eq(data.offending_request_len, 0);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(oa);
}

Test(openobserve_adapter, test_non_200_response_ignored)
{
  HttpAdapter *oa = openobserve_adapter_new();
  HttpResponseSignalData data =
  {
    .http_code = 401,
    .batch_size = 2,
    .request_body = g_string_new("msg1\nmsg2\n"),
    .response_body = g_string_new(
      "{\"code\":401,\"status\":[{\"name\":\"network\",\"successful\":0,\"failed\":2,\"error\":\"bad request\"}]}"
    ),
    .offending_message = 0,
    .offending_request_start = 0,
    .offending_request_len = 0,
  };

  http_adapter_adapt_response(oa, &data);

  cr_assert_eq(data.result, HTTP_SLOT_SUCCESS);
  /* Non-200 responses are not processed by this adapter */
  cr_assert_eq(data.offending_message, 0);
  cr_assert_eq(data.offending_request_start, 0);
  cr_assert_eq(data.offending_request_len, 0);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(oa);
}

Test(openobserve_adapter, test_invalid_json_handled_gracefully)
{
  HttpAdapter *oa = openobserve_adapter_new();
  HttpResponseSignalData data =
  {
    .http_code = 200,
    .batch_size = 2,
    .request_body = g_string_new("msg1\nmsg2\n"),
    .response_body = g_string_new("not valid json"),
    .offending_message = 0,
    .offending_request_start = 0,
    .offending_request_len = 0,
  };

  http_adapter_adapt_response(oa, &data);

  cr_assert_eq(data.result, HTTP_SLOT_CRITICAL_ERROR);
  cr_assert_eq(data.offending_message, 0);
  cr_assert_eq(data.offending_request_start, 0);
  cr_assert_eq(data.offending_request_len, 0);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(oa);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(openobserve_adapter, .init = setup, .fini = teardown);

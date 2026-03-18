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
#include "splunk-adapter.h"
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

Test(http_adapters, test_splunk_adapter_extract_offending_message)
{
  HttpAdapter *sa = splunk_adapter_new();
  HttpResponseSignalData data = {
    .http_code = 400,
    .batch_size = 4,
    .request_body = g_string_new("foo1\nfoo2\nfoo3\nfoo4\n"),
    .response_body = g_string_new("{\"text\":\"Event field cannot be blank\",\"code\":13,\"invalid-event-number\":1}"),
    0
  };

  http_adapter_adapt_response(sa, &data);

  cr_assert_eq(data.offending_message, 1);

  gchar *offending_request = _extract_offending_request(&data);
  cr_assert_str_eq(offending_request, "foo2");
  g_free(offending_request);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(sa);
}

Test(http_adapters, test_splunk_adapter_request_is_short)
{
  HttpAdapter *sa = splunk_adapter_new();
  HttpResponseSignalData data = {
    .http_code = 400,
    .batch_size = 4,
    /* request_body only has 3 entries */
    .request_body = g_string_new("foo1\nfoo2\nfoo3\n"),
    .response_body = g_string_new("{\"text\":\"Event field cannot be blank\",\"code\":13,\"invalid-event-number\":3}"),
    0
  };

  http_adapter_adapt_response(sa, &data);
  cr_assert_eq(data.offending_message, 3);
  cr_assert_eq(data.offending_request_start, 0);
  cr_assert_eq(data.offending_request_len, 0);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(sa);
}

Test(http_adapters, test_splunk_adapter_request_is_even_shorter)
{
  HttpAdapter *sa = splunk_adapter_new();
  HttpResponseSignalData data = {
    .http_code = 400,
    .batch_size = 5,
    /* request_body only has 3 entries */
    .request_body = g_string_new("foo1\nfoo2\n"),
    .response_body = g_string_new("{\"text\":\"Event field cannot be blank\",\"code\":13,\"invalid-event-number\":3}"),
    0
  };

  http_adapter_adapt_response(sa, &data);
  cr_assert_eq(data.offending_message, 3);
  cr_assert_eq(data.offending_request_start, 0);
  cr_assert_eq(data.offending_request_len, 0);

  g_string_free(data.request_body, TRUE);
  g_string_free(data.response_body, TRUE);
  http_adapter_free(sa);
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

TestSuite(http_adapters, .init = setup, .fini = teardown);

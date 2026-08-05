/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 László Várady
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

#include "http/http-parser.h"
#include "http/http-message.h"

#include <glib.h>

typedef struct _ExpectedHeader
{
  const gchar *key;
  const gchar *value;
} ExpectedHeader;

typedef struct _ExpectedHTTPRequest
{
  gint expected_http_major;
  gint expected_http_minor;

  ExpectedHeader *expected_headers;
  gsize expected_headers_length;

  const gchar *expected_body;
  const gchar *expected_url;
  const gchar *expected_method;
} ExpectedHTTPRequest;

static void
assert_print_parser_error(const HTTPParser *parser, gboolean condition)
{
  if (condition)
    return;

  GError *error = http_parser_get_last_error(parser);
  cr_assert_not_null(error);

  cr_assert(condition, "error: %s", error->message);

  g_error_free(error);
}

static void
assert_http_request(HTTPRequest *actual_request, ExpectedHTTPRequest *expected_request)
{
  http_request_null_terminate_body(actual_request);

  gushort http_major, http_minor;
  http_message_get_http_version(&actual_request->super, &http_major, &http_minor);
  cr_assert_eq(http_major, expected_request->expected_http_major);
  cr_assert_eq(http_minor, expected_request->expected_http_minor);

  for (gsize i = 0; i < expected_request->expected_headers_length; ++i)
    {
      GString *actual_value = http_message_get_header(&actual_request->super,
                                                      expected_request->expected_headers[i].key);
      cr_assert_not_null(actual_value);

      cr_assert_str_eq(actual_value->str, expected_request->expected_headers[i].value);
      g_string_free(actual_value, TRUE);
    }

  const gchar *actual_body = (const gchar *) http_message_get_body(&actual_request->super)->data;
  cr_assert_str_eq(actual_body, expected_request->expected_body);

  const gchar *actual_url = http_request_get_url(actual_request);
  cr_assert_str_eq(actual_url, expected_request->expected_url);

  const gchar *actual_method = http_request_get_method(actual_request);
  cr_assert_str_eq(actual_method, expected_request->expected_method);
}

static void
_test_parser_simple_request(const gchar *message, ExpectedHTTPRequest *expected_request)
{
  HTTPParser *parser = http_request_parser_new();

  gsize message_length = strlen(message);
  gsize consumed_bytes;

  assert_print_parser_error(parser, http_parser_feed(parser, message, message_length, &consumed_bytes));
  cr_assert(http_parser_is_message_complete(parser));
  cr_assert_eq(consumed_bytes, message_length);

  HTTPRequest *actual_request = (HTTPRequest *) http_parser_steal_message(parser);
  cr_assert_not_null(actual_request);

  assert_http_request(actual_request, expected_request);

  http_request_free(actual_request);
  http_parser_free(parser);
}

/*
 * chunk_lengths: zero-terminated array
 * if http_message is longer than sum(chunk_lengths), the remaining part will be fed as the last chunk
 */
static gboolean
_feed_entire_message_in_chunks(HTTPParser *parser, const gchar *http_message, const gsize *chunk_lengths)
{
  const gchar *remaining_message = http_message;
  gsize remaining_length = strlen(http_message);
  gsize consumed_bytes;

  for (const gsize *chunk_length = chunk_lengths; *chunk_length != 0; ++chunk_length)
    {
      cr_assert(remaining_length >= *chunk_length, "test case with invalid chunk_lengths array");

      if (!http_parser_feed(parser, remaining_message, *chunk_length, &consumed_bytes))
        return FALSE;

      remaining_message += *chunk_length;
      remaining_length -= *chunk_length;
    }

  if (!http_parser_feed(parser, remaining_message, remaining_length, &consumed_bytes))
    return FALSE;

  return TRUE;
}


Test(http_parser, test_simple_get_request)
{
  const gchar *request =
    "GET / HTTP/1.1\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 4\r\n"
    "\r\n"
    "deak";

  ExpectedHeader expected_headers[] =
  {
    { "Content-Type", "text/plain" },
    { "Content-Length", "4" }
  };

  ExpectedHTTPRequest expected =
  {
    .expected_http_major = 1,
    .expected_http_minor = 1,

    .expected_headers = expected_headers,
    .expected_headers_length = G_N_ELEMENTS(expected_headers),

    .expected_body = "deak",
    .expected_url = "/",
    .expected_method = "GET"
  };

  _test_parser_simple_request(request, &expected);
}

Test(http_parser, test_simple_post_request_feeding_the_parser_repeatedly)
{
  HTTPParser *parser = http_request_parser_new();

  const gchar *request =
    "POST /post_here/0404 HTTP/1.0\r\n"
    "Accept: */*\r\n"
    "Accept-Language: en-us,en;q=0.5\r\n"
    "Content-Length: 6\r\n"
    "\r\n"
    "ferenc";

  ExpectedHeader expected_headers[] =
  {
    { "Accept", "*/*" },
    { "Accept-Language", "en-us,en;q=0.5" },
    { "Content-Length", "6" }
  };

  ExpectedHTTPRequest expected_request =
  {
    .expected_http_major = 1,
    .expected_http_minor = 0,

    .expected_headers = expected_headers,
    .expected_headers_length = G_N_ELEMENTS(expected_headers),

    .expected_body = "ferenc",
    .expected_url = "/post_here/0404",
    .expected_method = "POST"
  };

  gboolean feed_success = _feed_entire_message_in_chunks(parser, request, (gsize[])
  {
    10, 40, 0
  });
  http_parser_signal_end_of_stream(parser);

  assert_print_parser_error(parser, feed_success);
  cr_assert(http_parser_is_message_complete(parser));

  HTTPMessage *msg = http_parser_steal_message(parser);

  assert_http_request((HTTPRequest *) msg, &expected_request);

  http_message_free(msg);
  http_parser_free(parser);
}

Test(http_parser, test_empty_message)
{
  HTTPParser *parser = http_request_parser_new();

  assert_print_parser_error(parser, http_parser_signal_end_of_stream(parser));
  cr_assert_not(http_parser_is_message_complete(parser));

  http_parser_free(parser);
}

Test(http_parser, test_continue_after_eos)
{
  HTTPParser *parser = http_request_parser_new();

  assert_print_parser_error(parser, http_parser_signal_end_of_stream(parser));
  cr_assert_not(http_parser_is_message_complete(parser));

  const gchar *request =
    "GET / HTTP/1.1\r\n"
    "\r\n";

  gsize request_length = strlen(request);
  gsize consumed_bytes;

  assert_print_parser_error(parser, http_parser_feed(parser, request, request_length, &consumed_bytes));
  cr_assert(http_parser_is_message_complete(parser));
  cr_assert_eq(consumed_bytes, request_length);

  http_parser_free(parser);
}

Test(http_parser, test_parser_should_pause_upon_message_completion)
{
  HTTPParser *parser = http_request_parser_new();

  const gchar *request =
    "GET / HTTP/1.1\r\n"
    "\r\n"
    "INVALID NEXT";

  gsize request_length = strlen(request);
  gsize consumed_bytes;

  assert_print_parser_error(parser, http_parser_feed(parser, request, request_length, &consumed_bytes));
  cr_assert(http_parser_is_message_complete(parser));
  cr_assert_neq(consumed_bytes, request_length);

  assert_print_parser_error(parser, http_parser_feed(parser, request, request_length, &consumed_bytes));
  cr_assert_eq(consumed_bytes, 0);

  http_parser_free(parser);
}

Test(http_parser, test_multiple_get_request)
{
  HTTPParser *parser = http_request_parser_new();

  const gchar *request =
    "GET / HTTP/1.1\r\n"
    "\r\n"
    "GET / HTTP/1.1\r\n"
    "\r\n";

  gsize request_length = strlen(request);
  gsize consumed_bytes;

  assert_print_parser_error(parser, http_parser_feed(parser, request, request_length, &consumed_bytes));
  cr_assert(http_parser_is_message_complete(parser));
  cr_assert_eq(consumed_bytes, request_length / 2);

  HTTPMessage *message = http_parser_steal_message(parser);
  http_message_free(message);

  assert_print_parser_error(parser, http_parser_feed(parser, request, request_length, &consumed_bytes));
  cr_assert(http_parser_is_message_complete(parser));
  cr_assert_eq(consumed_bytes, request_length / 2);

  http_parser_free(parser);
}

Test(http_parser, test_skip_message)
{
  HTTPParser *parser = http_request_parser_new();

  const gchar *request =
    "GET / HTTP/1.1\r\n"
    "\r\n";

  gsize request_length = strlen(request);
  gsize consumed_bytes;

  assert_print_parser_error(parser, http_parser_feed(parser, request, request_length, &consumed_bytes));
  cr_assert(http_parser_is_message_complete(parser));
  cr_assert_eq(consumed_bytes, request_length);

  http_parser_skip_message(parser);

  assert_print_parser_error(parser, http_parser_feed(parser, request, request_length, &consumed_bytes));
  cr_assert(http_parser_is_message_complete(parser));
  cr_assert_eq(consumed_bytes, request_length);

  http_parser_free(parser);
}

Test(http_parser, test_request_parser_with_response)
{
  HTTPParser *parser = http_request_parser_new();

  const gchar *response =
    "HTTP/1.1 200 OK\r\n"
    "\r\n";

  gsize response_length = strlen(response);
  gsize consumed_bytes;

  cr_assert_not(http_parser_feed(parser, response, response_length, &consumed_bytes));
  cr_assert_not(http_parser_is_message_complete(parser));

  http_parser_free(parser);
}

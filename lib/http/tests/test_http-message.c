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

#include "http/http-message-internal.h"
#include <glib.h>

Test(http_message, test_headers)
{
  HTTPRequest *request = http_request_new_empty();
  HTTPMessage *message = &request->super;
  request = NULL;

  const gchar *accept_key = "Accept";
  const gchar *accept_value = "text/html,application/xhtml+xml,application/xml;q=0.9";

  const gchar *accept_enc_key = "Accept-Encoding";
  const gchar *accept_enc_value = "gzip, deflate, br";

  http_message_add_header(message, accept_key, accept_value);
  http_message_add_header(message, accept_enc_key, accept_enc_value);

  GString *actual_value = http_message_get_header(message, accept_key);
  cr_assert_not_null(actual_value);
  cr_assert_str_eq(actual_value->str, accept_value);
  g_string_free(actual_value, TRUE);

  actual_value = http_message_get_header(message, accept_enc_key);
  cr_assert_not_null(actual_value);
  cr_assert_str_eq(actual_value->str, accept_enc_value);
  g_string_free(actual_value, TRUE);

  actual_value = http_message_get_header_using_normalized_key(message, "accept-encoding");
  cr_assert_not_null(actual_value);
  cr_assert_str_eq(actual_value->str, accept_enc_value);
  g_string_free(actual_value, TRUE);

  http_message_free(message);
}

Test(http_message, test_status_code_to_status_line)
{
  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_OK), "200 OK");
  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_MULTIPLE_CHOICES), "300 Multiple Choices");
  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_BAD_REQUEST), "400 Bad Request");
  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_INTERNAL_SERVER_ERROR), "500 Internal Server Error");

  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_ALREADY_REPORTED), "208 Already Reported");
  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_PERMANENT_REDIRECT), "308 Permanent Redirect");
  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE),
                   "431 Request Header Fields Too Large");
  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_NETWORK_AUTHENTICATION_REQUIRED),
                   "511 Network Authentication Required");

  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_NO_CONTENT), "204 No Content");
  cr_assert_str_eq(http_response_status_code_to_status_line(HTTP_FOUND), "302 Found");
  cr_assert_str_eq(http_response_status_code_to_status_line(404), "404 Not Found");
  cr_assert_str_eq(http_response_status_code_to_status_line(501), "501 Not Implemented");
}

Test(http_message, test_invalid_status_code_to_status_line)
{
  cr_assert_null(http_response_status_code_to_status_line(0));
  cr_assert_null(http_response_status_code_to_status_line(99));
  cr_assert_null(http_response_status_code_to_status_line(128));
  cr_assert_null(http_response_status_code_to_status_line(209));
  cr_assert_null(http_response_status_code_to_status_line(309));
  cr_assert_null(http_response_status_code_to_status_line(432));
  cr_assert_null(http_response_status_code_to_status_line(512));
  cr_assert_null(http_response_status_code_to_status_line(1024));
}

Test(http_message, test_generate_raw_response)
{
  HTTPResponse *response = http_response_new_empty();
  http_message_set_http_version(&response->super, 1, 1);
  http_response_set_status_code(response, HTTP_OK);
  http_message_add_header(&response->super, "Content-Length", "5");

  GByteArray *body = g_byte_array_new();
  g_byte_array_append(body, (const guint8 *) "hello", 5);

  http_message_take_body(&response->super, body);

  GByteArray *raw_response = http_response_generate_raw_response(response);

  const gchar *expected_response = "HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nhello";
  gsize expected_response_length = strlen(expected_response);
  cr_assert_arr_eq(raw_response->data, expected_response, expected_response_length);

  g_byte_array_free(raw_response, TRUE);
  http_response_free(response);
}

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

#include "logproto-http-server.h"
#include "buffer.h"
#include "http-parser.h"
#include "http-message-internal.h"
#include "syslog-ng.h"
#include "messages.h"

#include <errno.h>

typedef enum _State
{
  STATE_RECEIVE_HTTP_REQUEST,
  STATE_SEND_HTTP_RESPONSE,
  STATE_HTTP_ERROR,
  STATE_PROCESS_LOG_MESSAGES
} State;

typedef struct _ExtractLogMessagesCallback
{
  LPHTTPExtractLogMessagesFunc func;
  gpointer user_data;
} ExtractLogMessagesCallback;

typedef struct _CreateResponseCallback
{
  LPHTTPCreateResponseFunc func;
  gpointer user_data;
} CreateResponseCallback;

struct _LogProtoHTTPServer
{
  LogProtoServer super;

  State state;
  Buffer in_buffer;
  Buffer out_buffer;
  HTTPParser *http_parser;

  GQueue *pending_log_messages;

  // pass LogTransportAuxData
  ExtractLogMessagesCallback extract_log_messages;
  CreateResponseCallback create_response;
};

static LogProtoPrepareAction
log_proto_http_server_poll_prepare(LogProtoServer *s, GIOCondition *cond, gint *timeout)
{
  /* TODO timeout */

  LogProtoHTTPServer *self = (LogProtoHTTPServer *) s;

  if (self->state == STATE_PROCESS_LOG_MESSAGES)
    return LPPA_FORCE_SCHEDULE_FETCH;

  GIOCondition proto_io_direction;
  gboolean unprocessed_data_in_buffer;
  if (self->state == STATE_SEND_HTTP_RESPONSE || self->state == STATE_HTTP_ERROR)
    {
      unprocessed_data_in_buffer = buffer_size(&self->out_buffer) != 0;
      proto_io_direction = G_IO_OUT;
    }
  else if (self->state == STATE_RECEIVE_HTTP_REQUEST)
    {
      unprocessed_data_in_buffer = buffer_size(&self->in_buffer) != 0;
      proto_io_direction = G_IO_IN;
    }
  else
    g_assert_not_reached();

  if (log_transport_stack_poll_prepare(&self->super.transport_stack, cond))
    return LPPA_FORCE_SCHEDULE_FETCH;

  /* if there's no pending I/O in the transport layer */
  if (*cond == 0)
    *cond = proto_io_direction;

  return unprocessed_data_in_buffer ? LPPA_FORCE_SCHEDULE_FETCH : LPPA_POLL_IO;
}

static LogProtoStatus
_convert_io_status_to_log_proto_status(GIOStatus io_status)
{
  switch (io_status)
    {
    case G_IO_STATUS_NORMAL:
    case G_IO_STATUS_AGAIN:
      return LPS_SUCCESS;
    case G_IO_STATUS_ERROR:
      return LPS_ERROR;
    case G_IO_STATUS_EOF:
      return LPS_EOF;
    default:
      g_assert_not_reached();
    }
}

static GIOStatus
log_proto_http_server_fetch_data(LogProtoHTTPServer *self, LogTransportAuxData *aux)
{
  if (G_UNLIKELY(!buffer_allocated(&self->in_buffer)))
    buffer_allocate(&self->in_buffer, self->super.options->init_buffer_size);

  gssize rc = log_transport_stack_read(&self->super.transport_stack, buffer_end(&self->in_buffer),
                                       buffer_unused_capacity(&self->in_buffer), aux);

  if (rc < 0)
    {
      if (errno != EAGAIN)
        {
          msg_error("I/O error occurred while reading HTTP request",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
                    evt_tag_errno(EVT_TAG_OSERROR, errno));
          return G_IO_STATUS_ERROR;
        }
      else
        return G_IO_STATUS_AGAIN;
    }
  else if (rc == 0)
    {
      msg_verbose("EOF occurred while reading", evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return G_IO_STATUS_EOF;
    }

  buffer_increase_size(&self->in_buffer, rc);
  return G_IO_STATUS_NORMAL;
}

#define HTTP_ERROR_PAGE_FRONT "<html><head><title>syslog-ng</title></head><body><center><h1>"
#define HTTP_ERROR_PAGE_BACK "</h1></center><hr><center>syslog-ng</center></body></html>"

static GByteArray *
_generate_error_page(HTTPStatusCode error_code)
{
  const gchar *status_line = http_response_status_code_to_status_line(error_code);

  GByteArray *error_page = g_byte_array_sized_new(192);
  g_byte_array_append(error_page, (const guint8 *) HTTP_ERROR_PAGE_FRONT, sizeof(HTTP_ERROR_PAGE_FRONT) - 1);
  g_byte_array_append(error_page, (const guint8 *) status_line, strlen(status_line));
  g_byte_array_append(error_page, (const guint8 *) HTTP_ERROR_PAGE_BACK, sizeof(HTTP_ERROR_PAGE_BACK) - 1);

  return error_page;
}

static HTTPResponse *
_generate_error_response(HTTPStatusCode error_code)
{
  HTTPResponse *http_error_response = http_response_new_empty();

  http_message_set_http_version(&http_error_response->super, 1, 1);
  http_response_set_status_code(http_error_response, error_code);
  http_message_add_header(&http_error_response->super, "content-type", "text/html");
  http_message_add_header(&http_error_response->super, "connection", "close");

  GByteArray *error_page = _generate_error_page(error_code);
  http_message_take_body(&http_error_response->super, error_page);

  http_response_add_mandatory_headers(http_error_response);

  return http_error_response;
}

static void
log_proto_http_server_set_error_response(LogProtoHTTPServer *self, HTTPStatusCode error)
{
  HTTPResponse *http_error_response = _generate_error_response(error);

  GByteArray *raw_http_response = http_response_generate_raw_response(http_error_response);
  buffer_assign(&self->out_buffer, raw_http_response->data, raw_http_response->len);
  g_byte_array_free(raw_http_response, FALSE);

  http_response_free(http_error_response);

  self->state = STATE_HTTP_ERROR;
}

static HTTPRequest *
log_proto_http_server_parse_request(LogProtoHTTPServer *self, LogProtoStatus status)
{
  if (status == LPS_EOF)
    {
      if (!http_parser_signal_end_of_stream(self->http_parser))
        goto http_error;
    }
  else
    {
      const gchar *buf_start = (const gchar *) buffer_start(&self->in_buffer);
      gsize buf_bytes = buffer_size(&self->in_buffer);

      gsize consumed_bytes;
      if (!http_parser_feed(self->http_parser, buf_start, buf_bytes, &consumed_bytes))
        goto http_error;

      buffer_consume(&self->in_buffer, consumed_bytes);
    }

  if (http_parser_is_message_complete(self->http_parser))
    {
      buffer_split(&self->in_buffer);

      msg_debug("Incoming HTTP request");
      return (HTTPRequest *) http_parser_steal_message(self->http_parser);
    }

  if (self->in_buffer.size >= self->in_buffer.capacity) //TODO: self->super.super.options->max_msg_size
    {
      log_proto_http_server_set_error_response(self, HTTP_PAYLOAD_TOO_LARGE);
      msg_error("HTTP request is too long");
      return NULL;
    }

  return NULL;

http_error:
  log_proto_http_server_set_error_response(self, HTTP_BAD_REQUEST);

  GError *http_error = http_parser_get_last_error(self->http_parser);
  msg_error("Invalid HTTP request", evt_tag_str("http_error", http_error->message));
  g_error_free(http_error);

  return NULL;
}

static HTTPRequest *
log_proto_http_server_receive_request(LogProtoHTTPServer *self, LogTransportAuxData *aux, LogProtoStatus *status)
{
  /* This function returns when an HTTP request is complete or when error or EAGAIN occurs,
     so it can not be used as an "edge-triggered" reader */
  while (self->state == STATE_RECEIVE_HTTP_REQUEST)
    {
      if (buffer_is_empty(&self->in_buffer))
        {
          GIOStatus io_status = log_proto_http_server_fetch_data(self, aux);
          *status = _convert_io_status_to_log_proto_status(io_status);

          if (io_status == G_IO_STATUS_AGAIN || *status == LPS_ERROR)
            return NULL;
        }

      HTTPRequest *http_request = log_proto_http_server_parse_request(self, *status);
      if (http_request)
        return http_request;

      if (*status != LPS_SUCCESS)
        return NULL;
    }

  return NULL;
}

static GIOStatus
log_proto_http_server_flush_response(LogProtoHTTPServer *self)
{
  gssize rc = log_transport_stack_write(&self->super.transport_stack, buffer_start(&self->out_buffer),
                                        buffer_size(&self->out_buffer));

  if (rc < 0)
    {
      if (errno != EAGAIN)
        {
          msg_error("I/O error occurred while sending HTTP response",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
                    evt_tag_errno(EVT_TAG_OSERROR, errno));
          return G_IO_STATUS_ERROR;
        }
      else
        return G_IO_STATUS_AGAIN;
    }

  buffer_consume(&self->out_buffer, rc);
  return G_IO_STATUS_NORMAL;
}

static LogProtoStatus
log_proto_http_server_send_response(LogProtoHTTPServer *self)
{
  GIOStatus io_status = log_proto_http_server_flush_response(self);
  LogProtoStatus status = _convert_io_status_to_log_proto_status(io_status);

  if (!buffer_is_empty(&self->out_buffer))
    return status;

  buffer_deallocate(&self->out_buffer);

  if (self->state == STATE_HTTP_ERROR)
    return LPS_ERROR;

  self->state = STATE_RECEIVE_HTTP_REQUEST;
  return status;
}

static void
log_proto_http_server_extract_log_messages(LogProtoHTTPServer *self, HTTPRequest *http_request)
{
  if (!self->extract_log_messages.func)
    goto skip_log_message_processing;

  self->pending_log_messages = self->extract_log_messages.func(http_request, self->extract_log_messages.user_data);

  if (!self->pending_log_messages)
    goto skip_log_message_processing;

  if (g_queue_get_length(self->pending_log_messages) == 0)
    {
      g_queue_free(self->pending_log_messages);
      self->pending_log_messages = NULL;
      goto skip_log_message_processing;
    }

  self->state = STATE_PROCESS_LOG_MESSAGES;
  return;

skip_log_message_processing:
  self->state = STATE_SEND_HTTP_RESPONSE;
}

static void
log_proto_http_server_create_response(LogProtoHTTPServer *self, HTTPRequest *http_request)
{
  HTTPResponse *http_response = NULL;
  if (self->create_response.func)
    http_response = self->create_response.func(http_request, self->create_response.user_data);

  if (!http_response)
    {
      msg_error("No HTTP response, generating 'Internal Server Error' response");
      http_response = _generate_error_response(HTTP_INTERNAL_SERVER_ERROR);
    }

  http_response_add_mandatory_headers(http_response);

  GByteArray *raw_http_response = http_response_generate_raw_response(http_response);
  buffer_assign(&self->out_buffer, raw_http_response->data, raw_http_response->len);
  g_byte_array_free(raw_http_response, FALSE);

  http_response_free(http_response);
}

static void
log_proto_http_server_extract_log_messages_and_create_response(LogProtoHTTPServer *self, HTTPRequest *http_request)
{
  log_proto_http_server_extract_log_messages(self, http_request);
  log_proto_http_server_create_response(self, http_request);
}

static LogMessage *
log_proto_http_server_pop_next_log_message(LogProtoHTTPServer *self)
{
  LogMessage *log_message = g_queue_pop_head(self->pending_log_messages);
  if (!log_message)
    {
      g_queue_free(self->pending_log_messages);
      self->pending_log_messages = NULL;
      self->state = STATE_SEND_HTTP_RESPONSE;
    }

  return log_message;
}

#define MAX_REQUESTS_PER_FETCH 100

static LogProtoStatus
log_proto_http_server_process(LogProtoServer *s, LogMessage **log_message, LogTransportAuxData *aux, Bookmark *bookmark)
{
  LogProtoHTTPServer *self = (LogProtoHTTPServer *) s;
  LogProtoStatus status = LPS_SUCCESS;

  /* the loop decreases the load of main loop */
  for (gint requests = 0; requests < MAX_REQUESTS_PER_FETCH; )
    {
      switch (self->state)
        {
        case STATE_RECEIVE_HTTP_REQUEST:
          ;
          HTTPRequest *http_request = log_proto_http_server_receive_request(self, aux, &status);
          if (http_request)
            {
              log_proto_http_server_extract_log_messages_and_create_response(self, http_request);
              http_request_free(http_request);
            }
          else
            {
              return status;
            }
          break;

        case STATE_PROCESS_LOG_MESSAGES:
          *log_message = log_proto_http_server_pop_next_log_message(self);
          if (*log_message)
            return LPS_SUCCESS;
          break;

        case STATE_SEND_HTTP_RESPONSE:
        case STATE_HTTP_ERROR:
          status = log_proto_http_server_send_response(self);
          if (self->state != STATE_RECEIVE_HTTP_REQUEST)
            return status;
          requests++;
          break;

        default:
          g_assert_not_reached();
        }
    }

  if (status != LPS_SUCCESS)
    log_transport_aux_data_reinit(aux);

  return status;
}

static void
log_proto_http_server_free(LogProtoServer *s)
{
  LogProtoHTTPServer *self = (LogProtoHTTPServer *) s;

  buffer_deallocate(&self->in_buffer);
  buffer_deallocate(&self->out_buffer);
  http_parser_free(self->http_parser);

  log_proto_server_free_method(s);
}

void
log_proto_http_server_set_extract_log_messages(LogProtoServer *s,
                                               LPHTTPExtractLogMessagesFunc extract_log_messages, gpointer user_data)
{
  LogProtoHTTPServer *self = (LogProtoHTTPServer *) s;

  self->extract_log_messages.func = extract_log_messages;
  self->extract_log_messages.user_data = user_data;
}

void
log_proto_http_server_set_create_response(LogProtoServer *s, LPHTTPCreateResponseFunc create_response,
                                          gpointer user_data)
{
  LogProtoHTTPServer *self = (LogProtoHTTPServer *) s;

  self->create_response.func = create_response;
  self->create_response.user_data = user_data;
}

LogProtoServer *
log_proto_http_server_new(LogTransport *transport, const LogProtoServerOptions *options)
{
  LogProtoHTTPServer *self = g_new0(LogProtoHTTPServer, 1);

  log_proto_server_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_http_server_poll_prepare;
  self->super.fetch_structured = log_proto_http_server_process;
  self->super.free_fn = log_proto_http_server_free;

  self->http_parser = http_request_parser_new();

  return &self->super;
}

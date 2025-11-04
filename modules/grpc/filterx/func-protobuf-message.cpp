/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 shifter
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/filterx-protobuf-formatter.hpp"

#include "compat/cpp-start.h"
#include "func-protobuf-message.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "compat/cpp-end.h"

typedef struct FilterXProtobufMessage_
{
  FilterXFunction super;
  FilterXExpr *input;
  syslogng::grpc::FilterXProtobufFormatter *filterx_protobuf_formatter;
} FilterXProtobufMessage;

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXProtobufMessage *self = (FilterXProtobufMessage *) s;

  FilterXObject *input = filterx_expr_eval(self->input);
  if (!input)
    {
      filterx_eval_push_error_static_info("Failed to evaluate protobuf_message()", self->input,
                                          (gchar *) "Failed to evaluate input");
      return NULL;
    }

  FilterXObject *dict = filterx_ref_unwrap_ro(input);
  if (!filterx_object_is_type(dict, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate protobuf_message()", self->input,
                                          "Input must be a dict, got: %s",
                                          filterx_object_get_type_name(input));
      filterx_object_unref(input);
      return NULL;
    }

  google::protobuf::Message *message = NULL;
  try
    {
      message = self->filterx_protobuf_formatter->format(dict);
    }
  catch (const std::exception &ex)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate protobuf_message()", self->input,
                                          "Failed to format dict to protobuf message: %s",
                                          ex.what());
      filterx_object_unref(input);
      return NULL;
    }

  std::string protobuf_string = message->SerializeAsString();

  delete message;
  filterx_object_unref(input);

  return filterx_protobuf_new(protobuf_string.c_str(), protobuf_string.length());
}

static void
_free(FilterXExpr *s)
{
  FilterXProtobufMessage *self = (FilterXProtobufMessage *) s;

  delete self->filterx_protobuf_formatter;
  filterx_expr_unref(self->input);
  filterx_function_free_method(&self->super);
}

static gboolean
_extract_args(FilterXProtobufMessage *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_PROTOBUF_MESSAGE_USAGE);
      return FALSE;
    }

  self->input = filterx_function_args_get_expr(args, 0);
  if (!self->input)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "input must be set. " FILTERX_FUNC_PROTOBUF_MESSAGE_USAGE);
      return FALSE;
    }

  gboolean exists = FALSE;
  const gchar *proto_filename = filterx_function_args_get_named_literal_string(args,
                                FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA_FILE, NULL, &exists);
  if (exists && proto_filename != NULL)
    {
      std::string file_name(proto_filename);
      try
        {
          self->filterx_protobuf_formatter = new syslogng::grpc::FilterXProtobufFormatter(file_name);
        }
      catch(const std::exception &ex)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      "failed to load protobuf: %s " FILTERX_FUNC_PROTOBUF_MESSAGE_USAGE, ex.what());
          return FALSE;
        }
    }
  else
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "not yet implemented! " FILTERX_FUNC_PROTOBUF_MESSAGE_USAGE);
      return FALSE;
    }

  return TRUE;
}

static gboolean
_protobuf_message_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXProtobufMessage *self = (FilterXProtobufMessage *) s;

  FilterXExpr **exprs[] = { &self->input };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(exprs[i], f, user_data))
        return FALSE;
    }
  return TRUE;
}

FilterXExpr *
filterx_function_protobuf_message_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXProtobufMessage *self = g_new0(FilterXProtobufMessage, 1);
  filterx_function_init_instance(&self->super, "protobuf_message");

  self->super.super.eval = _eval;
  self->super.super.walk_children = _protobuf_message_walk;
  self->super.super.free_fn = _free;

  if (!_extract_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(protobuf_message, filterx_function_protobuf_message_new);

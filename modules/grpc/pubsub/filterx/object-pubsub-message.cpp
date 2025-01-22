/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 shifter
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

#include "object-pubsub-message.hpp"

#include "compat/cpp-start.h"

#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-datetime.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-object-istype.h"
#include "filterx/filterx-ref.h"
#include "scratch-buffers.h"
#include "generic-number.h"

#include "compat/cpp-end.h"

#include <unistd.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace syslogng::grpc::pubsub::filterx;

/* C++ Implementations */

Message::Message(FilterXPubSubMessage *super_) : super(super_)
{
}

Message::Message(FilterXPubSubMessage *super_, const std::string data,
                 const std::map<std::string, std::string> &attributes) : super(super_)
{
  message.set_data(data);
  for (const auto &pair : attributes)
    {
      const auto &key = pair.first;
      const auto &value = pair.second;

      (*message.mutable_attributes())[key] = value;
    }
}

Message::Message(FilterXPubSubMessage *super_, FilterXObject *protobuf_object) : super(super_)
{
  const gchar *value;
  gsize length;
  if (!filterx_object_extract_protobuf_ref(protobuf_object, &value, &length))
    throw std::runtime_error("Argument is not a protobuf object");

  if (!message.ParsePartialFromArray(value, length))
    throw std::runtime_error("Failed to parse from protobuf object");
}

Message::Message(const Message &o, FilterXPubSubMessage *super_) : super(super_),
  message(o.message)
{
}

std::string
Message::marshal(void)
{
  std::string serializedString = this->message.SerializePartialAsString();
  return serializedString;
}

const google::pubsub::v1::PubsubMessage &
Message::get_value() const
{
  return this->message;
}

bool
Message::set_attribute(const std::string &key, const std::string &value)
{
  try
    {
      if (key.empty())
        {
          throw std::invalid_argument("Key cannot be empty");
        }
      auto attributes = message.mutable_attributes();
      if (attributes)
        {
          (*attributes)[key] = value;
        }
      else
        {
          throw std::runtime_error("Failed to access mutable attributes");
        }
    }
  catch (const std::exception &e)
    {
      msg_error("FilterX: Unable to set Pub/Sub attribute", evt_tag_str("error", e.what()));
      return false;
    }
  return true;
}

bool
Message::set_data(const std::string &data)
{
  try
    {
      auto mdata = message.mutable_data();
      *mdata = data;
    }
  catch (const std::exception &e)
    {
      msg_error("FilterX: Unable to set Pub/Sub data", evt_tag_str("error", e.what()));
      return false;
    }
  return true;
}

// /* C Wrappers */

static void
_free(FilterXObject *s)
{
  FilterXPubSubMessage *self = (FilterXPubSubMessage *) s;

  delete self->cpp;
  self->cpp = NULL;

  filterx_object_free_method(s);
}

static gboolean
_truthy(FilterXObject *s)
{
  return TRUE;
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXPubSubMessage *self = (FilterXPubSubMessage *) s;

  std::string serialized = self->cpp->marshal();

  g_string_append_len(repr, serialized.c_str(), serialized.length());
  *t = LM_VT_PROTOBUF;
  return TRUE;
}

static gboolean
_map_to_json(FilterXObject *s, struct json_object **object, FilterXObject **assoc_object)
{
  FilterXPubSubMessage *self = (FilterXPubSubMessage *) s;

  *object = json_object_new_object();

  const std::string data_val = self->cpp->get_value().data();

  json_object_object_add(*object, "data", json_object_new_string(data_val.c_str()));

  struct json_object *attributes = json_object_new_object();

  for (const auto &pair : self->cpp->get_value().attributes())
    {
      const std::string &key = pair.first;
      const std::string &value = pair.second;
      json_object_object_add(attributes, key.c_str(), json_object_new_string(value.c_str()));
    }

  json_object_object_add(*object, "attributes", attributes);

  *assoc_object = filterx_json_new_from_object(json_object_get(*object));
  return TRUE;
}

static void
_init_instance(FilterXPubSubMessage *self)
{
  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(pubsub_message));
}

FilterXObject *
_filterx_pubsub_message_clone(FilterXObject *s)
{
  FilterXPubSubMessage *self = (FilterXPubSubMessage *) s;

  FilterXPubSubMessage *clone = g_new0(FilterXPubSubMessage, 1);
  _init_instance(clone);

  try
    {
      clone->cpp = new Message(*self->cpp, clone);
    }
  catch (const std::runtime_error &)
    {
      g_assert_not_reached();
    }

  return &clone->super;
}

gboolean
_get_repr(FilterXObject *obj, std::string &str, GString *repr)
{
  const gchar *buf = NULL;
  gsize len;
  if (filterx_object_extract_string_ref(obj, &buf, &len))
    {
      str = std::string(buf, len);
    }
  else
    {
      g_string_truncate(repr, 0);
      if (!filterx_object_repr(obj, repr))
        return FALSE;
      str = std::string(repr->str, repr->len);
    }
  return TRUE;
}

gboolean
_build_map(FilterXObject *key, FilterXObject *val, gpointer user_data)
{

  auto *msg = static_cast<syslogng::grpc::pubsub::filterx::Message *>(static_cast<gpointer *>(user_data)[0]);
  GString *buf = static_cast<GString *>(static_cast<gpointer *>(user_data)[1]);

  std::string key_cpp;
  std::string val_cpp;

  if (!_get_repr(key, key_cpp, buf))
    return FALSE;
  if (!_get_repr(val, val_cpp, buf))
    return FALSE;

  return msg->set_attribute(key_cpp, val_cpp);
}

FilterXObject *
filterx_pubsub_message_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXPubSubMessage *self = g_new0(FilterXPubSubMessage, 1);
  _init_instance(self);
  ScratchBuffersMarker m;
  GString *buf = scratch_buffers_alloc_and_mark(&m);
  try
    {
      if (!args || args_len == 0)
        {
          self->cpp = new Message(self);
        }
      else if (args_len == 2)
        {
          FilterXObject *data = args[0];
          FilterXObject *attributes = args[1];
          FilterXObject *attributes_arg = filterx_ref_unwrap_ro(attributes);
          if (filterx_object_is_type(attributes_arg, &FILTERX_TYPE_NAME(dict)))
            {
              self->cpp = new Message(self);
              std::string data_cpp;
              gsize len = 0;
              const gchar *data_str;
              if (filterx_object_extract_string_ref(data, &data_str, &len) ||
                  filterx_object_extract_bytes_ref(data, &data_str, &len) ||
                  filterx_object_extract_protobuf_ref(data, &data_str, &len)
                 )
                {
                  data_cpp = std::string(data_str, len);
                }
              else
                {
                  LogMessageValueType lmvt;
                  if (!filterx_object_marshal(data, buf, &lmvt))
                    {
                      throw std::runtime_error("unable to parse first argument as string! current type:" + std::string(
                                                 log_msg_value_type_to_str(lmvt)));
                    }
                  data_cpp = std::string(buf->str, buf->len);
                }
              self->cpp->set_data(data_cpp);

              gpointer user_data[] = {static_cast<gpointer>(self->cpp), static_cast<gpointer>(buf)};
              if (!filterx_dict_iter(attributes_arg, _build_map, user_data))
                {
                  throw std::runtime_error("dictionary argument iterator resulted with some error");
                }
            }
          else
            {
              throw std::runtime_error("Invalid type of arguments");
            }
        }
      else
        {
          throw std::runtime_error("Invalid number of arguments");
        }
      scratch_buffers_reclaim_marked(m);
    }
  catch (const std::runtime_error &e)
    {
      scratch_buffers_reclaim_marked(m);
      msg_error("FilterX: Failed to create Pub/Sub Message object", evt_tag_str("error", e.what()));
      filterx_object_unref(&self->super);
      return NULL;
    }

  scratch_buffers_reclaim_marked(m);
  return &self->super;
}

FILTERX_SIMPLE_FUNCTION(pubsub_message, filterx_pubsub_message_new_from_args);

FILTERX_DEFINE_TYPE(pubsub_message, FILTERX_TYPE_NAME(object),
                    .is_mutable = TRUE,
                    .marshal = _marshal,
                    .clone = _filterx_pubsub_message_clone,
                    .map_to_json = _map_to_json,
                    .truthy = _truthy,
                    .free_fn = _free,
                   );

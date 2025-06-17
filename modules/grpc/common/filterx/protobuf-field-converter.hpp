/*
 * Copyright (c) 2023 shifter
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

#ifndef PROTOBUF_FIELD_HPP
#define PROTOBUF_FIELD_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "filterx/filterx-object.h"
#include "filterx/object-list-interface.h"
#include "compat/cpp-end.h"

#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>
#include <google/protobuf/descriptor.h>

namespace syslogng {
namespace grpc {

struct ProtoReflectors
{
  const google::protobuf::Reflection *reflection;
  const google::protobuf::Descriptor *descriptor;
  const google::protobuf::FieldDescriptor *field_descriptor;
  google::protobuf::FieldDescriptor::Type field_type;
  ProtoReflectors(const google::protobuf::Message &message, const std::string &field_name)
  {
    this->reflection = message.GetReflection();
    this->descriptor = message.GetDescriptor();
    if (!this->reflection || !this->descriptor)
      {
        std::string error_msg = "unable to access reflector for protobuf message: "
                                + std::string(message.GetTypeName());
        throw std::invalid_argument(error_msg);
      }
    this->field_descriptor = this->descriptor->FindFieldByName(field_name);
    if (!this->field_descriptor)
      {
        std::string error_msg = "unknown field name: " + field_name;
        throw std::invalid_argument(error_msg);
      }
    this->field_type = this->field_descriptor->type();
    if (this->field_type >= google::protobuf::FieldDescriptor::MAX_TYPE ||
        this->field_type < 1)
      {
        std::string error_msg = "unknown field type: " + field_name + ", " +  std::to_string(this->field_type);
        throw std::invalid_argument(error_msg);
      }
  };

  const char *
  field_type_name() const
  {
#if GOOGLE_PROTOBUF_VERSION >= 6030000
    return this->field_descriptor->type_name().data();
#else
    return this->field_descriptor->type_name();
#endif
  }
};

class ProtobufFieldConverter
{
public:
  FilterXObject *get(google::protobuf::Message *message, const std::string &field_name)
  {
    try
      {
        ProtoReflectors reflectors(*message, field_name);
        return this->get(message, reflectors);
      }
    catch(const std::exception &ex)
      {
        msg_error("protobuf-field: Failed to get field:", evt_tag_str("message", ex.what()));
        return nullptr;
      }
  };

  bool set(google::protobuf::Message *message, const std::string &field_name, FilterXObject *object,
           FilterXObject **assoc_object)
  {
    try
      {
        ProtoReflectors reflectors(*message, field_name);
        if (this->set(message, reflectors, object, assoc_object))
          {
            if (!(*assoc_object))
              *assoc_object = filterx_object_ref(object);
            return true;
          }
        return false;
      }
    catch(const std::exception &ex)
      {
        msg_error("protobuf-field: Failed to set field:", evt_tag_str("message", ex.what()));
        return false;
      }
  }

  virtual bool set_repeated(google::protobuf::Message *message, const std::string &field_name, FilterXObject *object,
                            FilterXObject **assoc_object)
  {
    try
      {
        ProtoReflectors reflectors(*message, field_name);
        if (!reflectors.field_descriptor->is_repeated())
          {
            msg_error("protobuf-field: Failed to set repeated field, field is not repeated",
                      evt_tag_str("field", reflectors.field_type_name()));
            return false;
          }

        FilterXObject *list = filterx_ref_unwrap_ro(object);
        if (!filterx_object_is_type(list, &FILTERX_TYPE_NAME(list)))
          {
            msg_error("protobuf-field: Failed to set repeated field, object is not a list",
                      evt_tag_str("field", reflectors.field_type_name()),
                      evt_tag_str("type", list->type->name));
            return false;
          }

        reflectors.reflection->ClearField(message, reflectors.field_descriptor);

        guint64 len;
        g_assert(filterx_object_len(list, &len));

        for (gsize i = 0; i < len; i++)
          {
            FilterXObject *elem = filterx_list_get_subscript(list, i);

            if (!this->add(message, reflectors, elem))
              {
                msg_error("protobuf-field: Failed to add element to repeated field",
                          evt_tag_str("field", reflectors.field_type_name()),
                          evt_tag_str("type", elem->type->name));
                filterx_object_unref(elem);
                return false;
              }

            filterx_object_unref(elem);
          }

        *assoc_object = filterx_object_ref(object);
        return true;
      }
    catch(const std::exception &ex)
      {
        msg_error("protobuf-field: Failed to set repeated field:", evt_tag_str("message", ex.what()));
        return false;
      }
  }

  bool unset(google::protobuf::Message *message, const std::string &field_name)
  {
    try
      {
        ProtoReflectors reflectors(*message, field_name);
        reflectors.reflection->ClearField(message, reflectors.field_descriptor);
        return true;
      }
    catch(const std::exception &ex)
      {
        msg_error("protobuf-field: Failed to unset field:", evt_tag_str("message", ex.what()));
        return false;
      }
  }

  bool is_set(google::protobuf::Message *message, const std::string &field_name)
  {
    try
      {
        ProtoReflectors reflectors(*message, field_name);
        return reflectors.reflection->HasField(*message, reflectors.field_descriptor);
      }
    catch(const std::exception &ex)
      {
        msg_error("protobuf-field: Failed to check field:", evt_tag_str("message", ex.what()));
        return false;
      }
  }

  virtual ~ProtobufFieldConverter() {}

protected:
  virtual FilterXObject *get(google::protobuf::Message *message, ProtoReflectors reflectors) = 0;
  virtual bool set(google::protobuf::Message *message, ProtoReflectors reflectors,
                   FilterXObject *object, FilterXObject **assoc_object) = 0;
  virtual bool add(google::protobuf::Message *message, ProtoReflectors reflectors, FilterXObject *object) = 0;
};

class MapFieldConverter : public ProtobufFieldConverter
{
public:
  bool set_repeated(google::protobuf::Message *message, const std::string &field_name, FilterXObject *object,
                    FilterXObject **assoc_object);

  FilterXObject *get(google::protobuf::Message *message, ProtoReflectors reflectors);
  bool set(google::protobuf::Message *message, ProtoReflectors reflectors, FilterXObject *object,
           FilterXObject **assoc_object);
  bool add(google::protobuf::Message *message, ProtoReflectors reflectors, FilterXObject *object);
};

extern MapFieldConverter map_field_converter;

std::unique_ptr<ProtobufFieldConverter> *all_protobuf_converters();
ProtobufFieldConverter *get_protobuf_field_converter(google::protobuf::FieldDescriptor::Type field_type);

std::string extract_string_from_object(FilterXObject *object);

uint64_t get_protobuf_message_set_field_count(const google::protobuf::Message &message);


}
}

#endif

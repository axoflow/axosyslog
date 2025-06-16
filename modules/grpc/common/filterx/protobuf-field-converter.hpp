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
  const google::protobuf::FieldDescriptor *fieldDescriptor;
  google::protobuf::FieldDescriptor::Type fieldType;
  ProtoReflectors(const google::protobuf::Message &message, const std::string &fieldName)
  {
    this->reflection = message.GetReflection();
    this->descriptor = message.GetDescriptor();
    if (!this->reflection || !this->descriptor)
      {
        std::string error_msg = "unable to access reflector for protobuf message: "
                                + std::string(message.GetTypeName());
        throw std::invalid_argument(error_msg);
      }
    this->fieldDescriptor = this->descriptor->FindFieldByName(fieldName);
    if (!this->fieldDescriptor)
      {
        std::string error_msg = "unknown field name: " + fieldName;
        throw std::invalid_argument(error_msg);
      }
    this->fieldType = this->fieldDescriptor->type();
    if (this->fieldType >= google::protobuf::FieldDescriptor::MAX_TYPE ||
        this->fieldType < 1)
      {
        std::string error_msg = "unknown field type: " + fieldName + ", " +  std::to_string(this->fieldType);
        throw std::invalid_argument(error_msg);
      }
  };

  const char *
  field_type_name() const
  {
#if GOOGLE_PROTOBUF_VERSION >= 6030000
    return this->fieldDescriptor->type_name().data();
#else
    return this->fieldDescriptor->type_name();
#endif
  }
};

class ProtobufFieldConverter
{
public:
  FilterXObject *get(google::protobuf::Message *message, const std::string &fieldName)
  {
    try
      {
        ProtoReflectors reflectors(*message, fieldName);
        return this->get(message, reflectors);
      }
    catch(const std::exception &ex)
      {
        msg_error("protobuf-field: Failed to get field:", evt_tag_str("message", ex.what()));
        return nullptr;
      }
  };

  bool set(google::protobuf::Message *message, const std::string &fieldName, FilterXObject *object,
           FilterXObject **assoc_object)
  {
    try
      {
        ProtoReflectors reflectors(*message, fieldName);
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

  bool unset(google::protobuf::Message *message, const std::string &fieldName)
  {
    try
      {
        ProtoReflectors reflectors(*message, fieldName);
        reflectors.reflection->ClearField(message, reflectors.fieldDescriptor);
        return true;
      }
    catch(const std::exception &ex)
      {
        msg_error("protobuf-field: Failed to unset field:", evt_tag_str("message", ex.what()));
        return false;
      }
  }

  bool is_set(google::protobuf::Message *message, const std::string &fieldName)
  {
    try
      {
        ProtoReflectors reflectors(*message, fieldName);
        return reflectors.reflection->HasField(*message, reflectors.fieldDescriptor);
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
};

std::unique_ptr<ProtobufFieldConverter> *all_protobuf_converters();
ProtobufFieldConverter *get_protobuf_field_converter(google::protobuf::FieldDescriptor::Type fieldType);

std::string extract_string_from_object(FilterXObject *object);

uint64_t get_protobuf_message_set_field_count(const google::protobuf::Message &message);

}
}

#endif

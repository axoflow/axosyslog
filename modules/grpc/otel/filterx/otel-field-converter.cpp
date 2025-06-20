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

#include "syslog-ng.h"

#include "otel-field-converter.hpp"
#include "object-otel-kvlist.hpp"
#include "object-otel-array.hpp"

#include "compat/cpp-start.h"

#include "filterx/filterx-object.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-datetime.h"
#include "filterx/object-primitive.h"
#include "scratch-buffers.h"
#include "generic-number.h"
#include "filterx/object-message-value.h"
#include "filterx/object-null.h"

#include "compat/cpp-end.h"

#include "opentelemetry/proto/logs/v1/logs.pb.h"

#include <string.h>

using namespace syslogng::grpc;
using namespace syslogng::grpc::otel;
using namespace google::protobuf;
using namespace opentelemetry::proto::logs::v1;

gpointer
grpc_otel_filterx_enum_construct(Plugin *self)
{
  static FilterXEnumDefinition enums[] =
  {
    { "SEVERITY_NUMBER_TRACE", SeverityNumber::SEVERITY_NUMBER_TRACE },
    { "SEVERITY_NUMBER_TRACE2", SeverityNumber::SEVERITY_NUMBER_TRACE2 },
    { "SEVERITY_NUMBER_TRACE3", SeverityNumber::SEVERITY_NUMBER_TRACE3 },
    { "SEVERITY_NUMBER_TRACE4", SeverityNumber::SEVERITY_NUMBER_TRACE4 },
    { "SEVERITY_NUMBER_DEBUG", SeverityNumber::SEVERITY_NUMBER_DEBUG },
    { "SEVERITY_NUMBER_DEBUG2", SeverityNumber::SEVERITY_NUMBER_DEBUG2 },
    { "SEVERITY_NUMBER_DEBUG3", SeverityNumber::SEVERITY_NUMBER_DEBUG3 },
    { "SEVERITY_NUMBER_DEBUG4", SeverityNumber::SEVERITY_NUMBER_DEBUG4 },
    { "SEVERITY_NUMBER_INFO", SeverityNumber::SEVERITY_NUMBER_INFO },
    { "SEVERITY_NUMBER_INFO2", SeverityNumber::SEVERITY_NUMBER_INFO2 },
    { "SEVERITY_NUMBER_INFO3", SeverityNumber::SEVERITY_NUMBER_INFO3 },
    { "SEVERITY_NUMBER_INFO4", SeverityNumber::SEVERITY_NUMBER_INFO4 },
    { "SEVERITY_NUMBER_WARN", SeverityNumber::SEVERITY_NUMBER_WARN },
    { "SEVERITY_NUMBER_WARN2", SeverityNumber::SEVERITY_NUMBER_WARN2 },
    { "SEVERITY_NUMBER_WARN3", SeverityNumber::SEVERITY_NUMBER_WARN3 },
    { "SEVERITY_NUMBER_WARN4", SeverityNumber::SEVERITY_NUMBER_WARN4 },
    { "SEVERITY_NUMBER_ERROR", SeverityNumber::SEVERITY_NUMBER_ERROR },
    { "SEVERITY_NUMBER_ERROR2", SeverityNumber::SEVERITY_NUMBER_ERROR2 },
    { "SEVERITY_NUMBER_ERROR3", SeverityNumber::SEVERITY_NUMBER_ERROR3 },
    { "SEVERITY_NUMBER_ERROR4", SeverityNumber::SEVERITY_NUMBER_ERROR4 },
    { "SEVERITY_NUMBER_FATAL", SeverityNumber::SEVERITY_NUMBER_FATAL },
    { "SEVERITY_NUMBER_FATAL2", SeverityNumber::SEVERITY_NUMBER_FATAL2 },
    { "SEVERITY_NUMBER_FATAL3", SeverityNumber::SEVERITY_NUMBER_FATAL3 },
    { "SEVERITY_NUMBER_FATAL4", SeverityNumber::SEVERITY_NUMBER_FATAL4 },
    { NULL },
  };

  return enums;
}

FilterXObject *
AnyValueFieldConverter::get(Message *message, ProtoReflectors reflectors)
{
  if (reflectors.field_descriptor->type() == FieldDescriptor::TYPE_MESSAGE)
    {
      Message *nestedMessage = reflectors.reflection->MutableMessage(message, reflectors.field_descriptor);

      AnyValue *any_value;
      try
        {
          any_value = dynamic_cast<AnyValue *>(nestedMessage);
        }
      catch(const std::bad_cast &e)
        {
          g_assert_not_reached();
        }

      return this->direct_get(any_value);
    }

  msg_error("otel-field: Unexpected protobuf field type",
            evt_tag_str("name", reflectors.field_descriptor->name().data()),
            evt_tag_int("type", reflectors.field_type));
  return nullptr;
}

bool
AnyValueFieldConverter::set(Message *message, ProtoReflectors reflectors, FilterXObject *object,
                            FilterXObject **assoc_object)
{
  AnyValue *any_value;
  try
    {
      any_value = dynamic_cast<AnyValue *>(reflectors.reflection->MutableMessage(message, reflectors.field_descriptor));
    }
  catch(const std::bad_cast &e)
    {
      g_assert_not_reached();
    }

  return direct_set(any_value, object, assoc_object);
}

bool
AnyValueFieldConverter::add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
{
  throw std::runtime_error("AnyValueFieldConverter: add operation is not supported");
}

FilterXObject *
AnyValueFieldConverter::direct_get(AnyValue *any_value)
{
  ProtobufFieldConverter *converter = nullptr;
  std::string type_field_name;
  AnyValue::ValueCase valueCase = any_value->value_case();

  switch (valueCase)
    {
    case AnyValue::kStringValue:
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_STRING);
      type_field_name = "string_value";
      break;
    case AnyValue::kBoolValue:
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_BOOL);
      type_field_name = "bool_value";
      break;
    case AnyValue::kIntValue:
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_INT64);
      type_field_name = "int_value";
      break;
    case AnyValue::kDoubleValue:
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_DOUBLE);
      type_field_name = "double_value";
      break;
    case AnyValue::kBytesValue:
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_BYTES);
      type_field_name = "bytes_value";
      break;
    case AnyValue::kKvlistValue:
      converter = &filterx::kvlist_field_converter;
      type_field_name = "kvlist_value";
      break;
    case AnyValue::kArrayValue:
      converter = &filterx::array_field_converter;
      type_field_name = "array_value";
      break;
    case AnyValue::VALUE_NOT_SET:
      return filterx_null_new();
    default:
      g_assert_not_reached();
    }

  return converter->get(any_value, type_field_name.data());
}

bool
AnyValueFieldConverter::direct_set(AnyValue *any_value, FilterXObject *object, FilterXObject **assoc_object)
{
  ProtobufFieldConverter *converter = nullptr;
  const char *type_field_name;

  FilterXObject *object_unwrapped = filterx_ref_unwrap_ro(object);
  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(boolean)) ||
      (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
       filterx_message_value_get_type(object) == LM_VT_BOOLEAN))
    {
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_BOOL);
      type_field_name = "bool_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(integer))||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_INTEGER))
    {
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_INT64);
      type_field_name = "int_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(double)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_DOUBLE))
    {
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_DOUBLE);
      type_field_name = "double_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(string)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_STRING))
    {
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_STRING);
      type_field_name = "string_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(bytes)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_BYTES))
    {
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_BYTES);
      type_field_name = "bytes_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(protobuf)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_PROTOBUF))
    {
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_BYTES);
      type_field_name = "bytes_value";
    }
  else if (filterx_object_is_type(object_unwrapped, &FILTERX_TYPE_NAME(dict)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_JSON))
    {
      converter = &filterx::kvlist_field_converter;
      type_field_name = "kvlist_value";
    }
  else if (filterx_object_is_type(object_unwrapped, &FILTERX_TYPE_NAME(list)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_LIST))
    {
      converter = &filterx::array_field_converter;
      type_field_name = "array_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(datetime)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_DATETIME))
    {
      // notice int64_t (sfixed64) instead of uint64_t (fixed64) since anyvalue's int_value is int64_t
      converter = get_protobuf_field_converter(FieldDescriptor::TYPE_SFIXED64);
      type_field_name = "int_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(null)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_NULL))
    {
      any_value->clear_value();
      return true;
    }

  if (!converter)
    {
      msg_error("otel-field: FilterX type -> AnyValue field type conversion not yet implemented",
                evt_tag_str("type", object->type->name));
      return false;
    }

  return converter->set(any_value, type_field_name, object, assoc_object);
}

AnyValueFieldConverter syslogng::grpc::otel::any_value_field;

class DatetimeFieldConverter : public ProtobufFieldConverter
{
public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    uint64_t val = reflectors.reflection->GetUInt64(*message, reflectors.field_descriptor);
    UnixTime utime = unix_time_from_unix_epoch_nsec(val);
    return filterx_datetime_new(&utime);
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    UnixTime utime;
    if (filterx_object_extract_datetime(object, &utime))
      {
        uint64_t unix_epoch = unix_time_to_unix_epoch_nsec(utime);
        reflectors.reflection->SetUInt64(message, reflectors.field_descriptor, unix_epoch);
        return true;
      }

    return get_protobuf_field_converter(reflectors.field_descriptor->type())->set(message,
           std::string(reflectors.field_descriptor->name()), object, assoc_object);
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    throw std::runtime_error("DatetimeFieldConverter: add operation is not supported");
  }
};

static DatetimeFieldConverter datetime_field;

class SeverityNumberFieldConverter : public ProtobufFieldConverter
{
public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    int value = reflectors.reflection->GetEnumValue(*message, reflectors.field_descriptor);
    return filterx_integer_new(value);
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(integer)))
      {
        gint64 value;
        g_assert(filterx_integer_unwrap(object, &value));
        if (!SeverityNumber_IsValid((int) value))
          {
            msg_error("otel-field: Failed to set severity_number",
                      evt_tag_str("error", "Value is invalid"),
                      evt_tag_int("value", value));
            return false;
          }

        reflectors.reflection->SetEnumValue(message, reflectors.field_descriptor, (int) value);
        return true;
      }

    msg_error("otel-field: Failed to set severity_number",
              evt_tag_str("error", "Value type is invalid"),
              evt_tag_str("type", object->type->name));
    return false;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    throw std::runtime_error("SeverityNumberFieldConverter: add operation is not supported");
  }
};

static SeverityNumberFieldConverter severity_number_field;

ProtobufFieldConverter *
syslogng::grpc::otel::get_otel_protobuf_field_converter(FieldDescriptor::Type field_type)
{
  g_assert(field_type <= FieldDescriptor::MAX_TYPE && field_type > 0);
  if (field_type == FieldDescriptor::TYPE_MESSAGE)
    {
      return &any_value_field;
    }
  return all_protobuf_converters()[field_type - 1].get();
}

ProtobufFieldConverter *
syslogng::grpc::otel::get_otel_protobuf_field_converter(const FieldDescriptor *fd)
{
  const auto &field_name = fd->name();
  if (field_name.compare("time_unix_nano") == 0 ||
      field_name.compare("observed_time_unix_nano") == 0)
    {
      return &datetime_field;
    }

  if (field_name.compare("attributes") == 0)
    {
      return &filterx::kvlist_field_converter;
    }

  if (fd->type() == FieldDescriptor::TYPE_ENUM)
    {
      return &severity_number_field;
    }

  const FieldDescriptor::Type field_type = fd->type();
  return get_otel_protobuf_field_converter(field_type);
}

bool
syslogng::grpc::otel::iter_on_otel_protobuf_message_fields(google::protobuf::Message &message, FilterXDictIterFunc func,
                                                           void *user_data)
{
  const google::protobuf::Reflection *reflection = message.GetReflection();
  std::vector<const google::protobuf::FieldDescriptor *> fields;
  reflection->ListFields(message, &fields);

  try
    {
      for (const google::protobuf::FieldDescriptor *field : fields)
        {
          const std::string name = std::string(field->name());
          ProtoReflectors field_reflectors(message, name);
          ProtobufFieldConverter *converter = syslogng::grpc::otel::get_otel_protobuf_field_converter(
                                                field_reflectors.field_descriptor);

          FILTERX_STRING_DECLARE_ON_STACK(key, name.c_str(), name.size());
          FilterXObject *value = converter->get(&message, name);
          if (!value)
            {
              filterx_object_unref(key);
              return false;
            }

          bool success = func(key, value, user_data);

          filterx_object_unref(key);
          filterx_object_unref(value);

          if (!success)
            return false;
        }
    }
  catch (const std::exception &)
    {
      return false;
    }

  return true;
}

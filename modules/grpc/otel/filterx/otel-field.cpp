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

#include "otel-field.hpp"
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
AnyField::FilterXObjectGetter(Message *message, ProtoReflectors reflectors)
{
  if (reflectors.fieldDescriptor->type() == FieldDescriptor::TYPE_MESSAGE)
    {
      Message *nestedMessage = reflectors.reflection->MutableMessage(message, reflectors.fieldDescriptor);

      AnyValue *anyValue;
      try
        {
          anyValue = dynamic_cast<AnyValue *>(nestedMessage);
        }
      catch(const std::bad_cast &e)
        {
          g_assert_not_reached();
        }

      return this->FilterXObjectDirectGetter(anyValue);
    }

  msg_error("otel-field: Unexpected protobuf field type",
            evt_tag_str("name", reflectors.fieldDescriptor->name().data()),
            evt_tag_int("type", reflectors.fieldType));
  return nullptr;
}

bool
AnyField::FilterXObjectSetter(Message *message, ProtoReflectors reflectors, FilterXObject *object,
                              FilterXObject **assoc_object)
{
  AnyValue *anyValue;
  try
    {
      anyValue = dynamic_cast<AnyValue *>(reflectors.reflection->MutableMessage(message, reflectors.fieldDescriptor));
    }
  catch(const std::bad_cast &e)
    {
      g_assert_not_reached();
    }

  return FilterXObjectDirectSetter(anyValue, object, assoc_object);
}

FilterXObject *
AnyField::FilterXObjectDirectGetter(AnyValue *anyValue)
{
  ProtobufField *converter = nullptr;
  std::string typeFieldName;
  AnyValue::ValueCase valueCase = anyValue->value_case();

  switch (valueCase)
    {
    case AnyValue::kStringValue:
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_STRING);
      typeFieldName = "string_value";
      break;
    case AnyValue::kBoolValue:
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_BOOL);
      typeFieldName = "bool_value";
      break;
    case AnyValue::kIntValue:
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_INT64);
      typeFieldName = "int_value";
      break;
    case AnyValue::kDoubleValue:
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_DOUBLE);
      typeFieldName = "double_value";
      break;
    case AnyValue::kBytesValue:
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_BYTES);
      typeFieldName = "bytes_value";
      break;
    case AnyValue::kKvlistValue:
      converter = &filterx::otel_kvlist_converter;
      typeFieldName = "kvlist_value";
      break;
    case AnyValue::kArrayValue:
      converter = &filterx::otel_array_converter;
      typeFieldName = "array_value";
      break;
    case AnyValue::VALUE_NOT_SET:
      return filterx_null_new();
    default:
      g_assert_not_reached();
    }

  return converter->Get(anyValue, typeFieldName.data());
}

bool
AnyField::FilterXObjectDirectSetter(AnyValue *anyValue, FilterXObject *object, FilterXObject **assoc_object)
{
  ProtobufField *converter = nullptr;
  const char *typeFieldName;

  FilterXObject *object_unwrapped = filterx_ref_unwrap_ro(object);
  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(boolean)) ||
      (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
       filterx_message_value_get_type(object) == LM_VT_BOOLEAN))
    {
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_BOOL);
      typeFieldName = "bool_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(integer))||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_INTEGER))
    {
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_INT64);
      typeFieldName = "int_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(double)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_DOUBLE))
    {
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_DOUBLE);
      typeFieldName = "double_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(string)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_STRING))
    {
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_STRING);
      typeFieldName = "string_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(bytes)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_BYTES))
    {
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_BYTES);
      typeFieldName = "bytes_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(protobuf)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_PROTOBUF))
    {
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_BYTES);
      typeFieldName = "bytes_value";
    }
  else if (filterx_object_is_type(object_unwrapped, &FILTERX_TYPE_NAME(dict)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_JSON))
    {
      converter = &filterx::otel_kvlist_converter;
      typeFieldName = "kvlist_value";
    }
  else if (filterx_object_is_type(object_unwrapped, &FILTERX_TYPE_NAME(list)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_LIST))
    {
      converter = &filterx::otel_array_converter;
      typeFieldName = "array_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(datetime)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_DATETIME))
    {
      // notice int64_t (sfixed64) instead of uint64_t (fixed64) since anyvalue's int_value is int64_t
      converter = protobuf_converter_by_type(FieldDescriptor::TYPE_SFIXED64);
      typeFieldName = "int_value";
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(null)) ||
           (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
            filterx_message_value_get_type(object) == LM_VT_NULL))
    {
      anyValue->clear_value();
      return true;
    }

  if (!converter)
    {
      msg_error("otel-field: FilterX type -> AnyValue field type conversion not yet implemented",
                evt_tag_str("type", object->type->name));
      return false;
    }

  return converter->Set(anyValue, typeFieldName, object, assoc_object);
}

AnyField syslogng::grpc::otel::any_field_converter;

class OtelDatetimeConverter : public ProtobufField
{
public:
  FilterXObject *FilterXObjectGetter(Message *message, ProtoReflectors reflectors)
  {
    uint64_t val = reflectors.reflection->GetUInt64(*message, reflectors.fieldDescriptor);
    UnixTime utime = unix_time_from_unix_epoch_nsec(val);
    return filterx_datetime_new(&utime);
  }
  bool FilterXObjectSetter(Message *message, ProtoReflectors reflectors, FilterXObject *object,
                           FilterXObject **assoc_object)
  {
    UnixTime utime;
    if (filterx_object_extract_datetime(object, &utime))
      {
        uint64_t unix_epoch = unix_time_to_unix_epoch_nsec(utime);
        reflectors.reflection->SetUInt64(message, reflectors.fieldDescriptor, unix_epoch);
        return true;
      }

    return protobuf_converter_by_type(reflectors.fieldDescriptor->type())->Set(message,
           std::string(reflectors.fieldDescriptor->name()), object, assoc_object);
  }
};

static OtelDatetimeConverter otel_datetime_converter;

class OtelSeverityNumberEnumConverter : public ProtobufField
{
public:
  FilterXObject *FilterXObjectGetter(Message *message, ProtoReflectors reflectors)
  {
    int value = reflectors.reflection->GetEnumValue(*message, reflectors.fieldDescriptor);
    return filterx_integer_new(value);
  }
  bool FilterXObjectSetter(Message *message, ProtoReflectors reflectors, FilterXObject *object,
                           FilterXObject **assoc_object)
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

        reflectors.reflection->SetEnumValue(message, reflectors.fieldDescriptor, (int) value);
        return true;
      }

    msg_error("otel-field: Failed to set severity_number",
              evt_tag_str("error", "Value type is invalid"),
              evt_tag_str("type", object->type->name));
    return false;
  }
};

static OtelSeverityNumberEnumConverter otel_severity_number_enum_converter;

ProtobufField *syslogng::grpc::otel::otel_converter_by_type(FieldDescriptor::Type fieldType)
{
  g_assert(fieldType <= FieldDescriptor::MAX_TYPE && fieldType > 0);
  if (fieldType == FieldDescriptor::TYPE_MESSAGE)
    {
      return &any_field_converter;
    }
  return all_protobuf_converters()[fieldType - 1].get();
}

ProtobufField *syslogng::grpc::otel::otel_converter_by_field_descriptor(const FieldDescriptor *fd)
{
  const auto &fieldName = fd->name();
  if (fieldName.compare("time_unix_nano") == 0 ||
      fieldName.compare("observed_time_unix_nano") == 0)
    {
      return &otel_datetime_converter;
    }

  if (fieldName.compare("attributes") == 0)
    {
      return &filterx::otel_kvlist_converter;
    }

  if (fd->type() == FieldDescriptor::TYPE_ENUM)
    {
      return &otel_severity_number_enum_converter;
    }

  const FieldDescriptor::Type fieldType = fd->type();
  return otel_converter_by_type(fieldType);
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
          ProtobufField *converter = syslogng::grpc::otel::otel_converter_by_field_descriptor(field_reflectors.fieldDescriptor);

          FILTERX_STRING_DECLARE_ON_STACK(key, name.c_str(), name.size());
          FilterXObject *value = converter->Get(&message, name);
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

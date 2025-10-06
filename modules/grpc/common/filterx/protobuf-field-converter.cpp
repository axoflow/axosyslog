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

#include "protobuf-field-converter.hpp"

#include "compat/cpp-start.h"

#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-message-value.h"
#include "filterx/object-datetime.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict.h"
#include "scratch-buffers.h"
#include "generic-number.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/json-repr.h"
#include "filterx/object-null.h"
#include "compat/cpp-end.h"

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace syslogng::grpc;
using google::protobuf::Message;

void
log_type_error(ProtoReflectors reflectors, FilterXObject *object)
{
  filterx_eval_push_error_info_printf("Failed to convert field", NULL,
                                      "Type for field %s is unsupported: %s",
                                      reflectors.field_descriptor->name().data(),
                                      filterx_object_get_type_name(object));
}

float
double_to_float_safe(double val)
{
  if (val < (double)(-FLT_MAX))
    return -FLT_MAX;
  else if (val > (double)(FLT_MAX))
    return FLT_MAX;
  return (float)val;
}

/* C++ Implementations */

// ProtoReflectors reflectors
class BoolFieldConverter : public ProtobufFieldConverter
{
private:
  static gboolean extract(FilterXObject *object)
  {
    return filterx_object_truthy(object);
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    return filterx_boolean_new(reflectors.reflection->GetBool(*message, reflectors.field_descriptor));
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    reflectors.reflection->SetBool(message, reflectors.field_descriptor, this->extract(object));
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    reflectors.reflection->AddBool(message, reflectors.field_descriptor, this->extract(object));
    return true;
  }
};

class i32FieldConverter : public ProtobufFieldConverter
{
private:
  static int32_t extract(FilterXObject *object)
  {
    gint64 i;
    if (filterx_object_extract_integer(object, &i))
      return MAX(INT32_MIN, MIN(INT32_MAX, i));
    throw std::runtime_error("i32FieldConverter: unsupported type");
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    return filterx_integer_new(gint32(reflectors.reflection->GetInt32(*message, reflectors.field_descriptor)));
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    try
      {
        reflectors.reflection->SetInt32(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    try
      {
        reflectors.reflection->AddInt32(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }
};

class i64FieldConverter : public ProtobufFieldConverter
{
private:
  static int64_t extract(FilterXObject *object)
  {
    gint64 i;
    if (filterx_object_extract_integer(object, &i))
      return i;

    UnixTime utime;
    if (filterx_object_extract_datetime(object, &utime))
      return (int64_t)unix_time_to_unix_epoch_usec(utime);

    throw std::runtime_error("i64FieldConverter: unsupported type");
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    return filterx_integer_new(gint64(reflectors.reflection->GetInt64(*message, reflectors.field_descriptor)));
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    try
      {
        reflectors.reflection->SetInt64(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    try
      {
        reflectors.reflection->AddInt64(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }
};

class u32FieldConverter : public ProtobufFieldConverter
{
private:
  static uint32_t extract(FilterXObject *object)
  {
    gint64 i;
    if (filterx_object_extract_integer(object, &i))
      return MAX(0, MIN(UINT32_MAX, i));
    throw std::runtime_error("u32FieldConverter: unsupported type");
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    return filterx_integer_new(guint32(reflectors.reflection->GetUInt32(*message, reflectors.field_descriptor)));
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    try
      {
        reflectors.reflection->SetUInt32(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    try
      {
        reflectors.reflection->AddUInt32(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }
};

class u64FieldConverter : public ProtobufFieldConverter
{
private:
  static uint64_t extract(FilterXObject *object)
  {
    gint64 i;
    if (filterx_object_extract_integer(object, &i))
      return MAX(0, MIN(UINT64_MAX, (uint64_t)i));
    throw std::runtime_error("u64FieldConverter: unsupported type");
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    uint64_t val = reflectors.reflection->GetUInt64(*message, reflectors.field_descriptor);
    if (val > INT64_MAX)
      {
        filterx_eval_push_error_info_printf("Failed to convert field", NULL,
                                            "Value of field %s is exceeding FilterX integer value range: "
                                            "min: %" G_GINT64_FORMAT ", "
                                            "max: %" G_GINT64_FORMAT ", "
                                            "value: %" G_GUINT64_FORMAT,
                                            reflectors.field_descriptor->name().data(), INT64_MIN, INT64_MAX, val);
        return NULL;
      }
    return filterx_integer_new(guint64(val));
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    try
      {
        reflectors.reflection->SetUInt64(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    try
      {
        reflectors.reflection->AddUInt64(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }
};

class StringFieldConverter : public ProtobufFieldConverter
{
private:
  static std::string extract(FilterXObject *object, ProtoReflectors reflectors)
  {
    const gchar *str;
    gsize len;

    if (filterx_object_extract_string_ref(object, &str, &len))
      return std::string(str, len);

    if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)) &&
        filterx_message_value_get_type(object) == LM_VT_JSON)
      {
        str = filterx_message_value_get_value(object, &len);
        return std::string(str, len);
      }

    object = filterx_ref_unwrap_ro(object);
    if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(dict)) ||
        filterx_object_is_type(object, &FILTERX_TYPE_NAME(list)))
      {
        GString *repr = scratch_buffers_alloc();
        if (!filterx_object_to_json(object, repr))
          throw std::runtime_error("StringFieldConverter: json marshal error");
        return std::string(repr->str, repr->len);
      }

    throw std::runtime_error("StringFieldConverter: unsupported type");
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    std::string bytes_buffer;
    const google::protobuf::Reflection *reflection = reflectors.reflection;
    const std::string &bytes_ref = reflection->GetStringReference(*message, reflectors.field_descriptor, &bytes_buffer);
    return filterx_string_new(bytes_ref.c_str(), bytes_ref.length());
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    try
      {
        reflectors.reflection->SetString(message, reflectors.field_descriptor, this->extract(object, reflectors));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    try
      {
        reflectors.reflection->AddString(message, reflectors.field_descriptor, this->extract(object, reflectors));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }
};

class DoubleFieldConverter : public ProtobufFieldConverter
{
private:
  static double extract(FilterXObject *object)
  {
    gint64 i;
    if (filterx_object_extract_integer(object, &i))
      return static_cast<double>(i);

    gdouble d;
    if (filterx_object_extract_double(object, &d))
      return d;

    throw std::runtime_error("DoubleFieldConverter: unsupported type");
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    return filterx_double_new(gdouble(reflectors.reflection->GetDouble(*message, reflectors.field_descriptor)));
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    try
      {
        reflectors.reflection->SetDouble(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    try
      {
        reflectors.reflection->AddDouble(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }
};

class FloatFieldConverter : public ProtobufFieldConverter
{
private:
  static float extract(FilterXObject *object)
  {
    gint64 i;
    if (filterx_object_extract_integer(object, &i))
      return double_to_float_safe(static_cast<double>(i));

    gdouble d;
    if (filterx_object_extract_double(object, &d))
      return double_to_float_safe(d);

    throw std::runtime_error("FloatFieldConverter: unsupported type");
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    return filterx_double_new(gdouble(reflectors.reflection->GetFloat(*message, reflectors.field_descriptor)));
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    try
      {
        reflectors.reflection->SetFloat(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    try
      {
        reflectors.reflection->AddFloat(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }
};

class BytesFieldConverter : public ProtobufFieldConverter
{
private:
  static std::string extract(FilterXObject *object)
  {
    const gchar *str;
    gsize len;

    if (filterx_object_extract_bytes_ref(object, &str, &len) ||
        filterx_object_extract_protobuf_ref(object, &str, &len))
      return std::string{str, len};

    throw std::runtime_error("BytesFieldConverter: unsupported type");
  }

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    std::string bytes_buffer;
    const google::protobuf::Reflection *reflection = reflectors.reflection;
    const std::string &bytes_ref = reflection->GetStringReference(*message, reflectors.field_descriptor, &bytes_buffer);
    return filterx_bytes_new(bytes_ref.c_str(), bytes_ref.length());
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    try
      {
        reflectors.reflection->SetString(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    try
      {
        reflectors.reflection->AddString(message, reflectors.field_descriptor, this->extract(object));
      }
    catch (const std::exception &e)
      {
        log_type_error(reflectors, object);
        return false;
      }
    return true;
  }
};

FilterXObject *
MapFieldConverter::get(Message *message, ProtoReflectors reflectors)
{
  const gchar *key_name = "key";
  const gchar *value_name = "value";

  FilterXObject *dict = filterx_dict_new();

  int len = reflectors.reflection->FieldSize(*message, reflectors.field_descriptor);
  for (int i = 0; i < len; i++)
    {
      Message *elem_message = reflectors.reflection->MutableRepeatedMessage(message, reflectors.field_descriptor, i);
      ProtoReflectors key_reflectors(*elem_message, key_name);
      ProtoReflectors value_reflectors(*elem_message, value_name);

      ProtobufFieldConverter *key_converter = get_protobuf_field_converter(key_reflectors.field_type);
      ProtobufFieldConverter *value_converter = get_protobuf_field_converter(value_reflectors.field_type);

      FilterXObject *key_object = key_converter->get(elem_message, key_name);
      if (!key_object)
        return NULL;

      FilterXObject *value_object = value_converter->get(elem_message, value_name);
      if (!value_object)
        {
          filterx_object_unref(key_object);
          return NULL;
        }

      if (!filterx_object_set_subscript(dict, key_object, &value_object))
        {
          filterx_object_unref(key_object);
          filterx_object_unref(value_object);
          return NULL;
        }

      filterx_object_unref(key_object);
      filterx_object_unref(value_object);
    }

  return dict;
}

static gboolean
_map_add_elem(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  google::protobuf::Message *message = static_cast<google::protobuf::Message *>(((gpointer *) user_data)[0]);
  ProtoReflectors *reflectors = static_cast<ProtoReflectors *>(((gpointer *) user_data)[1]);

  const gchar *key_name = "key";
  const gchar *value_name = "value";

  try
    {
      Message *elem_message = reflectors->reflection->AddMessage(message, reflectors->field_descriptor);
      ProtoReflectors key_reflectors(*elem_message, key_name);
      ProtoReflectors value_reflectors(*elem_message, value_name);

      ProtobufFieldConverter *key_converter = get_protobuf_field_converter(key_reflectors.field_type);
      ProtobufFieldConverter *value_converter = get_protobuf_field_converter(value_reflectors.field_type);

      FilterXObject *assoc_key_object = NULL;
      if (!key_converter->set(elem_message, key_name, key, &assoc_key_object))
        return FALSE;

      filterx_object_unref(assoc_key_object);

      FilterXObject *assoc_val_object = NULL;
      if (!value_converter->set(elem_message, value_name, value, &assoc_val_object))
        return FALSE;

      filterx_object_unref(assoc_val_object);
    }
  catch (const std::exception &e)
    {
      return FALSE;
    }

  return TRUE;
}

bool
MapFieldConverter::set_repeated(Message *message, const std::string &field_name, FilterXObject *object,
                                FilterXObject **assoc_object)
{
  ProtoReflectors reflectors(*message, field_name);

  FilterXObject *dict = filterx_ref_unwrap_ro(object);
  if (!filterx_object_is_type(dict, &FILTERX_TYPE_NAME(dict)))
    {
      log_type_error(reflectors, object);
      return false;
    }

  gpointer user_data[] = { message, &reflectors };
  return filterx_object_iter(dict, _map_add_elem, user_data);
}

bool
MapFieldConverter::set(Message *message, ProtoReflectors reflectors, FilterXObject *object,
                       FilterXObject **assoc_object)
{
  /* Map is always repeated. */
  g_assert_not_reached();
}

bool
MapFieldConverter::add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
{
  /* Map has its own set_repeated implementation. */
  g_assert_not_reached();
}

static gboolean
_message_add_elem(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  google::protobuf::Message *message = static_cast<google::protobuf::Message *>(user_data);

  const gchar *key_c_str;
  gsize key_len;
  if (!filterx_object_extract_string_ref(key, &key_c_str, &key_len))
    return FALSE;

  std::string key_string{key_c_str, key_len};

  try
    {
      ProtoReflectors elem_reflectors(*message, key_string);
      ProtobufFieldConverter *converter = get_protobuf_field_converter(elem_reflectors.field_type);

      FilterXObject *assoc_key_object = NULL;
      if (!converter->set(message, key_string, value, &assoc_key_object))
        {
          filterx_object_unref(assoc_key_object);
          return FALSE;
        }

      filterx_object_unref(assoc_key_object);
    }
  catch (const std::exception &e)
    {
      filterx_eval_push_error_info_printf("Failed to add element to message field", NULL,
                                          "key: %s, value type: %s, error: %s",
                                          key_c_str, filterx_object_get_type_name(value), e.what());
      return FALSE;
    }

  return TRUE;
}

class MessageFieldConverter : public ProtobufFieldConverter
{
public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors)
  {
    if (reflectors.field_descriptor->is_map())
      return map_field_converter.get(message, reflectors);

    FilterXObject *dict = filterx_dict_new();

    int len = reflectors.reflection->FieldSize(*message, reflectors.field_descriptor);
    for (int i = 0; i < len; i++)
      {
        Message *elem_message = reflectors.reflection->MutableRepeatedMessage(message, reflectors.field_descriptor, i);

        const std::string field_name = std::string(reflectors.field_descriptor->name());
        ProtoReflectors elem_reflectors(*message, field_name);
        ProtobufFieldConverter *converter = get_protobuf_field_converter(elem_reflectors.field_type);

        FilterXObject *value_object = converter->get(elem_message, field_name);
        if (!value_object)
          return NULL;

        FilterXObject *key_object = filterx_string_new(field_name.c_str(), field_name.length());
        if (!filterx_object_set_subscript(dict, key_object, &value_object))
          {
            filterx_object_unref(key_object);
            filterx_object_unref(value_object);
            return NULL;
          }

        filterx_object_unref(key_object);
        filterx_object_unref(value_object);
      }

    return dict;
  }

  bool set_repeated(Message *message, const std::string &field_name, FilterXObject *object,
                    FilterXObject **assoc_object)
  {
    ProtoReflectors reflectors(*message, field_name);

    if (reflectors.field_descriptor->is_map())
      return map_field_converter.set_repeated(message, field_name, object, assoc_object);

    return ProtobufFieldConverter::set_repeated(message, field_name, object, assoc_object);
  }

  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object)
  {
    FilterXObject *dict = filterx_ref_unwrap_ro(object);
    if (!filterx_object_is_type(dict, &FILTERX_TYPE_NAME(dict)))
      {
        log_type_error(reflectors, object);
        return false;
      }

    Message *inner_message = reflectors.reflection->MutableMessage(message, reflectors.field_descriptor);
    gpointer user_data = inner_message;
    return filterx_object_iter(dict, _message_add_elem, user_data);
  }

  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object)
  {
    FilterXObject *dict = filterx_ref_unwrap_ro(object);
    if (!filterx_object_is_type(dict, &FILTERX_TYPE_NAME(dict)))
      {
        log_type_error(reflectors, object);
        return false;
      }

    Message *inner_message = reflectors.reflection->AddMessage(message, reflectors.field_descriptor);
    gpointer user_data = inner_message;
    return filterx_object_iter(dict, _message_add_elem, user_data);
  }
};

MapFieldConverter syslogng::grpc::map_field_converter;

std::unique_ptr<ProtobufFieldConverter> *
syslogng::grpc::all_protobuf_converters()
{
  static std::unique_ptr<ProtobufFieldConverter> Converters[google::protobuf::FieldDescriptor::MAX_TYPE] =
  {
    std::make_unique<DoubleFieldConverter>(),  // TYPE_DOUBLE = 1,       double, exactly eight bytes on the wire.
    std::make_unique<FloatFieldConverter>(),   // TYPE_FLOAT = 2,        float, exactly four bytes on the wire.
    std::make_unique<i64FieldConverter>(),     // TYPE_INT64 = 3,        int64, varint on the wire.
    //                                                                   Negative numbers take 10 bytes.
    //                                                                   Use TYPE_SINT64 if negative values are likely.
    std::make_unique<u64FieldConverter>(),     // TYPE_UINT64 = 4,       uint64, varint on the wire.
    std::make_unique<i32FieldConverter>(),     // TYPE_INT32 = 5,        int32, varint on the wire.
    //                                                                   Negative numbers take 10 bytes.
    //                                                                   Use TYPE_SINT32 if negative values are likely.
    std::make_unique<u64FieldConverter>(),     // TYPE_FIXED64 = 6,      uint64, exactly eight bytes on the wire.
    std::make_unique<u32FieldConverter>(),     // TYPE_FIXED32 = 7,      uint32, exactly four bytes on the wire.
    std::make_unique<BoolFieldConverter>(),    // TYPE_BOOL = 8,         bool, varint on the wire.
    std::make_unique<StringFieldConverter>(),  // TYPE_STRING = 9,       UTF-8 text.
    nullptr,                                   // TYPE_GROUP = 10,       Tag-delimited message.  Deprecated.
    std::make_unique<MessageFieldConverter>(), // TYPE_MESSAGE = 11,     Length-delimited message.
    std::make_unique<BytesFieldConverter>(),   // TYPE_BYTES = 12,       Arbitrary byte array.
    std::make_unique<u32FieldConverter>(),     // TYPE_UINT32 = 13,      uint32, varint on the wire
    nullptr,                                   // TYPE_ENUM = 14,        Enum, varint on the wire
    std::make_unique<i32FieldConverter>(),     // TYPE_SFIXED32 = 15,    int32, exactly four bytes on the wire
    std::make_unique<i64FieldConverter>(),     // TYPE_SFIXED64 = 16,    int64, exactly eight bytes on the wire
    std::make_unique<i32FieldConverter>(),     // TYPE_SINT32 = 17,      int32, ZigZag-encoded varint on the wire
    std::make_unique<i64FieldConverter>(),     // TYPE_SINT64 = 18,      int64, ZigZag-encoded varint on the wire
  };
  return Converters;
};

ProtobufFieldConverter *
syslogng::grpc::get_protobuf_field_converter(google::protobuf::FieldDescriptor::Type field_type)
{
  g_assert(field_type <= google::protobuf::FieldDescriptor::MAX_TYPE && field_type > 0);
  return all_protobuf_converters()[field_type - 1].get();
}

std::string
syslogng::grpc::extract_string_from_object(FilterXObject *object)
{
  const gchar *key_c_str;
  gsize len;

  if (!filterx_object_extract_string_ref(object, &key_c_str, &len))
    throw std::runtime_error("not a string instance");

  return std::string{key_c_str, len};
}

uint64_t
syslogng::grpc::get_protobuf_message_set_field_count(const Message &message)
{
  const google::protobuf::Reflection *reflection = message.GetReflection();
  std::vector<const google::protobuf::FieldDescriptor *> fields;
  reflection->ListFields(message, &fields);
  return fields.size();
}

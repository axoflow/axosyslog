/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "object-otel-array.hpp"
#include "otel-field-converter.hpp"

#include "compat/cpp-start.h"
#include "filterx/object-extractor.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-message-value.h"
#include "filterx/filterx-eval.h"
#include "compat/cpp-end.h"

#include <google/protobuf/reflection.h>
#include <google/protobuf/util/json_util.h>
#include <stdexcept>

using namespace syslogng::grpc::otel::filterx;
using opentelemetry::proto::common::v1::AnyValue;

/* C++ Implementations */

Array::Array(FilterXOtelArray *s) :
  super(s),
  array(new ArrayValue()),
  borrowed(false)
{
}

Array::Array(FilterXOtelArray *s, ArrayValue *a) :
  super(s),
  array(a),
  borrowed(true)
{
}

Array::Array(const Array &o, FilterXOtelArray *s) :
  super(s),
  array(new ArrayValue()),
  borrowed(false)
{
  array->CopyFrom(*o.array);
}

Array::Array(FilterXOtelArray *s, FilterXObject *protobuf_object) :
  super(s),
  array(new ArrayValue()),
  borrowed(false)
{
  const gchar *value;
  gsize length;
  if (!filterx_object_extract_protobuf_ref(protobuf_object, &value, &length))
    {
      delete array;
      throw std::runtime_error("Argument is not a protobuf object");
    }

  if (!array->ParsePartialFromArray(value, length))
    {
      delete array;
      throw std::runtime_error("Failed to parse from protobuf object");
    }
}

Array::~Array()
{
  if (!borrowed)
    delete array;
}

std::string
Array::marshal(void)
{
  return array->SerializePartialAsString();
}

bool
Array::set_subscript(guint64 index, FilterXObject **value)
{
  FilterXObject *assoc_object = NULL;
  if (!any_value_field.direct_set(array->mutable_values(index), *value, &assoc_object))
    return false;

  filterx_object_unref(*value);
  *value = assoc_object;
  return true;
}

bool
Array::append(FilterXObject **value)
{
  FilterXObject *assoc_object = NULL;
  if (!any_value_field.direct_set(array->add_values(), *value, &assoc_object))
    return false;

  filterx_object_unref(*value);
  *value = assoc_object;
  return true;
}

bool
Array::unset_index(guint64 index)
{
  array->mutable_values()->DeleteSubrange(index, 1);
  return true;
}

FilterXObject *
Array::get_subscript(guint64 index)
{
  AnyValue *any_value = array->mutable_values(index);
  return any_value_field.direct_get(any_value);
}

guint64
Array::len() const
{
  return (guint64) array->values_size();
}

const ArrayValue &
Array::get_value() const
{
  return *array;
}

const google::protobuf::Message &
Array::get_protobuf_value() const
{
  return get_value();
}

/* C Wrappers */

static void
_free(FilterXObject *s)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  delete self->cpp;
  self->cpp = NULL;

  filterx_object_free_method(s);
}

static gboolean
_set_subscript(FilterXList *s, guint64 index, FilterXObject **new_value)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  return self->cpp->set_subscript(index, new_value);
}

static gboolean
_append(FilterXList *s, FilterXObject **new_value)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  return self->cpp->append(new_value);
}

static FilterXObject *
_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  guint64 normalized_index;
  const gchar *error;
  if (!filterx_list_normalize_index(key, self->cpp->len(), &normalized_index, FALSE, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return NULL;
    }

  return self->cpp->get_subscript(normalized_index);
}

static gboolean
_unset_index(FilterXList *s, guint64 index)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  return self->cpp->unset_index(index);
}

static guint64
_len(FilterXList *s)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  return self->cpp->len();
}

static gboolean
_truthy(FilterXObject *s)
{
  return TRUE;
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  std::string serialized = self->cpp->marshal();

  g_string_truncate(repr, 0);
  g_string_append_len(repr, serialized.c_str(), serialized.length());
  *t = LM_VT_PROTOBUF;
  return TRUE;
}

static void
_init_instance(FilterXOtelArray *self)
{
  filterx_list_init_instance(&self->super, &FILTERX_TYPE_NAME(otel_array));

  self->super.set_subscript = _set_subscript;
  self->super.append = _append;
  self->super.unset_index = _unset_index;
  self->super.len = _len;
}

FilterXObject *
_filterx_otel_array_clone(FilterXObject *s)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  FilterXOtelArray *clone = g_new0(FilterXOtelArray, 1);
  _init_instance(clone);

  try
    {
      clone->cpp = new Array(*self->cpp, clone);
    }
  catch (const std::runtime_error &)
    {
      g_assert_not_reached();
    }

  return &clone->super.super;
}

FilterXObject *
filterx_otel_array_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXOtelArray *self = g_new0(FilterXOtelArray, 1);
  _init_instance(self);

  try
    {
      if (!args || args_len == 0)
        {
          self->cpp = new Array(self);
        }
      else if (args_len == 1)
        {
          FilterXObject *arg = args[0];

          FilterXObject *list_arg = filterx_ref_unwrap_ro(arg);
          if (filterx_object_is_type(list_arg, &FILTERX_TYPE_NAME(list)))
            {
              self->cpp = new Array(self);
              if (!filterx_list_merge(&self->super.super, list_arg))
                throw std::runtime_error("Failed to merge list");
            }
          else
            {
              self->cpp = new Array(self, arg);
            }
        }
      else
        {
          throw std::runtime_error("Invalid number of arguments");
        }
    }
  catch (const std::runtime_error &e)
    {
      filterx_eval_push_error_info_printf("Failed to create OTel Array object", s, "%s", e.what());
      filterx_object_unref(&self->super.super);
      return NULL;
    }

  return &self->super.super;
}

static FilterXObject *
_new_borrowed(ArrayValue *array)
{
  FilterXOtelArray *self = g_new0(FilterXOtelArray, 1);
  _init_instance(self);

  self->cpp = new Array(self, array);

  return &self->super.super;
}

FILTERX_SIMPLE_FUNCTION(otel_array, filterx_otel_array_new_from_args);

FilterXObject *
ArrayFieldConverter::get(google::protobuf::Message *message, ProtoReflectors reflectors)
{
  try
    {
      Message *nestedMessage = reflectors.reflection->MutableMessage(message, reflectors.field_descriptor);
      ArrayValue *array = dynamic_cast<ArrayValue *>(nestedMessage);
      return _new_borrowed(array);
    }
  catch(const std::bad_cast &e)
    {
      g_assert_not_reached();
    }
}

static ArrayValue *
_get_array_value(google::protobuf::Message *message, syslogng::grpc::ProtoReflectors reflectors)
{
  try
    {
      return dynamic_cast<ArrayValue *>(reflectors.reflection->MutableMessage(message, reflectors.field_descriptor));
    }
  catch(const std::bad_cast &e)
    {
      g_assert_not_reached();
    }
}

static bool
_set_array_field_from_list(google::protobuf::Message *message, syslogng::grpc::ProtoReflectors reflectors,
                           FilterXObject *object, FilterXObject **assoc_object)
{
  ArrayValue *array = _get_array_value(message, reflectors);

  array->Clear();

  guint64 len;
  g_assert(filterx_object_len(object, &len));

  for (guint64 i = 0; i < len; i++)
    {
      FilterXObject *value_obj = filterx_list_get_subscript(object, (gint64) MIN(i, G_MAXINT64));

      AnyValue *any_value = array->add_values();

      FilterXObject *elem_assoc_object = NULL;
      if (!syslogng::grpc::otel::any_value_field.direct_set(any_value, value_obj, &elem_assoc_object))
        {
          filterx_object_unref(value_obj);
          return false;
        }

      filterx_object_unref(elem_assoc_object);
      filterx_object_unref(value_obj);
    }

  *assoc_object = _new_borrowed(array);
  return true;
}

bool
ArrayFieldConverter::set(google::protobuf::Message *message, ProtoReflectors reflectors,
                         FilterXObject *object, FilterXObject **assoc_object)
{
  FilterXObject *object_unwrapped = filterx_ref_unwrap_rw(object);
  if (!filterx_object_is_type(object_unwrapped, &FILTERX_TYPE_NAME(otel_array)))
    {
      if (filterx_object_is_type(object_unwrapped, &FILTERX_TYPE_NAME(list)))
        return _set_array_field_from_list(message, reflectors, object_unwrapped, assoc_object);

      if (filterx_object_is_type(object_unwrapped, &FILTERX_TYPE_NAME(message_value)))
        {
          FilterXObject *unmarshalled = filterx_object_unmarshal(object_unwrapped);
          bool success = filterx_object_is_type(unmarshalled, &FILTERX_TYPE_NAME(list)) &&
                         _set_array_field_from_list(message, reflectors, unmarshalled, assoc_object);
          filterx_object_unref(unmarshalled);
          return success;
        }

      filterx_eval_push_error_info_printf("Failed to convert field", NULL,
                                          "Type for field %s must be list or otel_array, got: %s",
                                          reflectors.field_type_name(),
                                          filterx_object_get_type_name(object));
      return false;
    }

  FilterXOtelArray *filterx_array = (FilterXOtelArray *) object_unwrapped;

  ArrayValue *array_value = _get_array_value(message, reflectors);
  array_value->CopyFrom(filterx_array->cpp->get_value());

  Array *new_array;
  try
    {
      new_array = new Array(filterx_array, array_value);
    }
  catch (const std::runtime_error &)
    {
      g_assert_not_reached();
    }

  delete filterx_array->cpp;
  filterx_array->cpp = new_array;

  return true;
}

bool
ArrayFieldConverter::add(google::protobuf::Message *message, ProtoReflectors reflectors, FilterXObject *object)
{
  throw std::runtime_error("DatetimeFieldConverter: add operation is not supported");
}

ArrayFieldConverter syslogng::grpc::otel::filterx::array_field_converter;

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  FilterXOtelArray *self = (FilterXOtelArray *) s;

  try
    {
      std::string cstring = self->cpp->repr();
      g_string_assign(repr, cstring.c_str());
    }
  catch (const std::runtime_error &e)
    {
      filterx_eval_push_error_info_printf("Failed to call repr() on OTel Array object", NULL, "%s", e.what());
      return FALSE;
    }

  return TRUE;
}

FILTERX_DEFINE_TYPE(otel_array, FILTERX_TYPE_NAME(list),
                    .is_mutable = TRUE,
                    .marshal = _marshal,
                    .clone = _filterx_otel_array_clone,
                    .truthy = _truthy,
                    .get_subscript = _get_subscript,
                    .repr = _repr,
                    .free_fn = _free,
                   );

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

#include "object-otel-logrecord.hpp"
#include "filterx/protobuf-field-converter.hpp"
#include "otel-field-converter.hpp"

#include "compat/cpp-start.h"

#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-datetime.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"
#include "generic-number.h"

#include "compat/cpp-end.h"

#include <google/protobuf/util/json_util.h>

#include <unistd.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace syslogng::grpc::otel::filterx;

/* C++ Implementations */

LogRecord::LogRecord(FilterXOtelLogRecord *super_) : super(super_)
{
}

LogRecord::LogRecord(FilterXOtelLogRecord *super_, FilterXObject *protobuf_object) : super(super_)
{
  const gchar *value;
  gsize length;
  if (!filterx_object_extract_protobuf_ref(protobuf_object, &value, &length))
    throw std::runtime_error("Argument is not a protobuf object");

  if (!logRecord.ParsePartialFromArray(value, length))
    throw std::runtime_error("Failed to parse from protobuf object");
}

LogRecord::LogRecord(const LogRecord &o, FilterXOtelLogRecord *super_) : super(super_),
  logRecord(o.logRecord)
{
}

std::string
LogRecord::marshal(void)
{
  std::string serializedString = this->logRecord.SerializePartialAsString();
  return serializedString;
}

bool
LogRecord::set_subscript(FilterXObject *key, FilterXObject **value)
{
  try
    {
      std::string key_str = extract_string_from_object(key);
      ProtoReflectors reflectors(this->logRecord, key_str);
      ProtobufFieldConverter *converter = get_otel_protobuf_field_converter(reflectors.field_descriptor);

      FilterXObject *assoc_object = NULL;
      if (!converter->set(&this->logRecord, key_str, *value, &assoc_object))
        return false;

      filterx_object_unref(*value);
      *value = assoc_object;
      return true;
    }
  catch(const std::exception &ex)
    {
      return false;
    }
}

FilterXObject *
LogRecord::get_subscript(FilterXObject *key)
{
  try
    {
      std::string key_str = extract_string_from_object(key);
      ProtoReflectors reflectors(this->logRecord, key_str);
      ProtobufFieldConverter *converter = get_otel_protobuf_field_converter(reflectors.field_descriptor);

      return converter->get(&this->logRecord, key_str);
    }
  catch(const std::exception &ex)
    {
      return nullptr;
    }
}

bool
LogRecord::unset_key(FilterXObject *key)
{
  try
    {
      std::string key_str = extract_string_from_object(key);
      ProtoReflectors reflectors(this->logRecord, key_str);
      ProtobufFieldConverter *converter = get_otel_protobuf_field_converter(reflectors.field_descriptor);

      return converter->unset(&this->logRecord, key_str);
    }
  catch(const std::exception &ex)
    {
      return false;
    }
}

bool
LogRecord::is_key_set(FilterXObject *key)
{
  try
    {
      std::string key_str = extract_string_from_object(key);
      ProtoReflectors reflectors(this->logRecord, key_str);
      ProtobufFieldConverter *converter = get_otel_protobuf_field_converter(reflectors.field_descriptor);

      return converter->is_set(&this->logRecord, key_str);
    }
  catch(const std::exception &ex)
    {
      return false;
    }
}

uint64_t
LogRecord::len() const
{
  return get_protobuf_message_set_field_count(logRecord);
}

bool
LogRecord::iter(FilterXDictIterFunc func, void *user_data)
{
  return iter_on_otel_protobuf_message_fields(logRecord, func, user_data);
}

const opentelemetry::proto::logs::v1::LogRecord &
LogRecord::get_value() const
{
  return this->logRecord;
}

const google::protobuf::Message &
LogRecord::get_protobuf_value() const
{
  return get_value();
}


/* C Wrappers */


static void
_free(FilterXObject *s)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  delete self->cpp;
  self->cpp = NULL;

  filterx_object_free_method(s);
}

static gboolean
_set_subscript(FilterXDict *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  return self->cpp->set_subscript(key, new_value);
}

static FilterXObject *
_get_subscript(FilterXDict *s, FilterXObject *key)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  return self->cpp->get_subscript(key);
}

static gboolean
_unset_key(FilterXDict *s, FilterXObject *key)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  return self->cpp->unset_key(key);
}

static gboolean
_is_key_set(FilterXDict *s, FilterXObject *key)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  return self->cpp->is_key_set(key);
}

static guint64
_len(FilterXDict *s)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  return self->cpp->len();
}

static gboolean
_iter(FilterXDict *s, FilterXDictIterFunc func, gpointer user_data)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  return self->cpp->iter(func, user_data);
}

static gboolean
_truthy(FilterXObject *s)
{
  return TRUE;
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  std::string serialized = self->cpp->marshal();

  g_string_truncate(repr, 0);
  g_string_append_len(repr, serialized.c_str(), serialized.length());
  *t = LM_VT_PROTOBUF;
  return TRUE;
}

static void
_init_instance(FilterXOtelLogRecord *self)
{
  filterx_dict_init_instance(&self->super, &FILTERX_TYPE_NAME(otel_logrecord));

  self->super.get_subscript = _get_subscript;
  self->super.set_subscript = _set_subscript;
  self->super.unset_key = _unset_key;
  self->super.is_key_set = _is_key_set;
  self->super.len = _len;
  self->super.iter = _iter;
}

FilterXObject *
_filterx_otel_logrecord_clone(FilterXObject *s)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  FilterXOtelLogRecord *clone = g_new0(FilterXOtelLogRecord, 1);
  _init_instance(clone);

  try
    {
      clone->cpp = new LogRecord(*self->cpp, clone);
    }
  catch (const std::runtime_error &)
    {
      g_assert_not_reached();
    }

  return &clone->super.super;
}

FilterXObject *
filterx_otel_logrecord_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXOtelLogRecord *self = g_new0(FilterXOtelLogRecord, 1);
  _init_instance(self);

  try
    {
      if (!args || args_len == 0)
        {
          self->cpp = new LogRecord(self);
        }
      else if (args_len == 1)
        {
          FilterXObject *arg = args[0];
          FilterXObject *dict_arg = filterx_ref_unwrap_ro(arg);
          if (filterx_object_is_type(dict_arg, &FILTERX_TYPE_NAME(dict)))
            {
              self->cpp = new LogRecord(self);
              if (!filterx_dict_merge(&self->super.super, dict_arg))
                throw std::runtime_error("Failed to merge dict");
            }
          else
            {
              self->cpp = new LogRecord(self, arg);
            }
        }
      else
        {
          throw std::runtime_error("Invalid number of arguments");
        }
    }
  catch (const std::runtime_error &e)
    {
      filterx_eval_push_error_info_printf("Failed to create OTel LogRecord object", s, "%s", e.what());
      filterx_object_unref(&self->super.super);
      return NULL;
    }

  return &self->super.super;
}

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  FilterXOtelLogRecord *self = (FilterXOtelLogRecord *) s;

  try
    {
      std::string cstring = self->cpp->repr();
      g_string_assign(repr, cstring.c_str());
    }
  catch (const std::runtime_error &e)
    {
      filterx_eval_push_error_info_printf("Failed to call repr() on OTel LogRecord object", NULL, "%s", e.what());
      return FALSE;
    }
  return TRUE;
}

FILTERX_SIMPLE_FUNCTION(otel_logrecord, filterx_otel_logrecord_new_from_args);

FILTERX_DEFINE_TYPE(otel_logrecord, FILTERX_TYPE_NAME(dict),
                    .is_mutable = TRUE,
                    .marshal = _marshal,
                    .clone = _filterx_otel_logrecord_clone,
                    .truthy = _truthy,
                    .repr = _repr,
                    .free_fn = _free,
                   );

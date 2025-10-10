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

#include "object-otel-scope.hpp"
#include "otel-field-converter.hpp"

#include "compat/cpp-start.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "compat/cpp-end.h"

#include <google/protobuf/util/json_util.h>
#include <stdexcept>

using namespace syslogng::grpc::otel::filterx;

/* C++ Implementations */

Scope::Scope(FilterXOtelScope *s) : super(s)
{
}

Scope::Scope(FilterXOtelScope *s, FilterXObject *protobuf_object) : super(s)
{
  const gchar *value;
  gsize length;
  if (!filterx_object_extract_protobuf_ref(protobuf_object, &value, &length))
    throw std::runtime_error("Argument is not a protobuf object");

  if (!scope.ParsePartialFromArray(value, length))
    throw std::runtime_error("Failed to parse from protobuf object");
}

Scope::Scope(const Scope &o, FilterXOtelScope *s) : super(s), scope(o.scope)
{
}

std::string
Scope::marshal(void)
{
  return scope.SerializePartialAsString();
}

bool
Scope::set_subscript(FilterXObject *key, FilterXObject **value)
{
  try
    {
      std::string key_str = extract_string_from_object(key);
      ProtoReflectors reflectors(scope, key_str);
      ProtobufFieldConverter *converter = get_otel_protobuf_field_converter(reflectors.field_descriptor);

      FilterXObject *assoc_object = NULL;
      if (!converter->set(&scope, key_str, *value, &assoc_object))
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
Scope::get_subscript(FilterXObject *key)
{
  try
    {
      std::string key_str = extract_string_from_object(key);
      ProtoReflectors reflectors(scope, key_str);
      ProtobufFieldConverter *converter = get_otel_protobuf_field_converter(reflectors.field_descriptor);

      return converter->get(&scope, key_str);
    }
  catch(const std::exception &ex)
    {
      return nullptr;
    }
}

bool
Scope::unset_key(FilterXObject *key)
{
  try
    {
      std::string key_str = extract_string_from_object(key);
      ProtoReflectors reflectors(scope, key_str);
      ProtobufFieldConverter *converter = get_otel_protobuf_field_converter(reflectors.field_descriptor);

      return converter->unset(&scope, key_str);
    }
  catch(const std::exception &ex)
    {
      return false;
    }
}

bool
Scope::is_key_set(FilterXObject *key)
{
  try
    {
      std::string key_str = extract_string_from_object(key);
      ProtoReflectors reflectors(scope, key_str);
      ProtobufFieldConverter *converter = get_otel_protobuf_field_converter(reflectors.field_descriptor);

      return converter->is_set(&scope, key_str);
    }
  catch(const std::exception &ex)
    {
      return false;
    }
}

uint64_t
Scope::len() const
{
  return get_protobuf_message_set_field_count(scope);
}

bool
Scope::iter(FilterXObjectIterFunc func, void *user_data)
{
  return iter_on_otel_protobuf_message_fields(scope, func, user_data);
}

const opentelemetry::proto::common::v1::InstrumentationScope &
Scope::get_value() const
{
  return scope;
}

const google::protobuf::Message &
Scope::get_protobuf_value() const
{
  return get_value();
}

/* C Wrappers */

static void
_free(FilterXObject *s)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  delete self->cpp;
  self->cpp = NULL;

  filterx_object_free_method(s);
}

static gboolean
_set_subscript(FilterXObject *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  const gchar *error;
  if (!filterx_mapping_normalize_key(key, NULL, NULL, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return FALSE;
    }
  return self->cpp->set_subscript(key, new_value);
}

static FilterXObject *
_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  const gchar *error;
  if (!filterx_mapping_normalize_key(key, NULL, NULL, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return NULL;
    }
  return self->cpp->get_subscript(key);
}

static gboolean
_unset_key(FilterXObject *s, FilterXObject *key)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  const gchar *error;
  if (!filterx_mapping_normalize_key(key, NULL, NULL, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return FALSE;
    }

  return self->cpp->unset_key(key);
}

static gboolean
_is_key_set(FilterXObject *s, FilterXObject *key)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  const gchar *error;
  if (!filterx_mapping_normalize_key(key, NULL, NULL, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return FALSE;
    }
  return self->cpp->is_key_set(key);
}

static gboolean
_len(FilterXObject *s, guint64 *len)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  *len = self->cpp->len();
  return TRUE;
}

static gboolean
_iter(FilterXObject *s, FilterXObjectIterFunc func, gpointer user_data)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

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
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  std::string serialized = self->cpp->marshal();

  g_string_truncate(repr, 0);
  g_string_append_len(repr, serialized.c_str(), serialized.length());
  *t = LM_VT_PROTOBUF;
  return TRUE;
}

static void
_init_instance(FilterXOtelScope *self)
{
  filterx_mapping_init_instance(&self->super, &FILTERX_TYPE_NAME(otel_scope));

}

FilterXObject *
_filterx_otel_scope_clone(FilterXObject *s)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  FilterXOtelScope *clone = g_new0(FilterXOtelScope, 1);
  _init_instance(clone);

  try
    {
      clone->cpp = new Scope(*self->cpp, self);
    }
  catch (const std::runtime_error &)
    {
      g_assert_not_reached();
    }

  return &clone->super.super;
}

FilterXObject *
filterx_otel_scope_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXOtelScope *self = g_new0(FilterXOtelScope, 1);
  _init_instance(self);

  try
    {
      if (!args || args_len == 0)
        {
          self->cpp = new Scope(self);
        }
      else if (args_len == 1)
        {
          FilterXObject *arg = args[0];
          FilterXObject *dict_arg = filterx_ref_unwrap_ro(arg);
          if (filterx_object_is_type(dict_arg, &FILTERX_TYPE_NAME(mapping)))
            {
              self->cpp = new Scope(self);
              if (!filterx_mapping_merge(&self->super.super, dict_arg))
                throw std::runtime_error("Failed to merge dict");
            }
          else
            {
              self->cpp = new Scope(self, arg);
            }
        }
      else
        {
          throw std::runtime_error("Invalid number of arguments");
        }
    }
  catch (const std::runtime_error &e)
    {
      filterx_eval_push_error_info_printf("Failed to create OTel Scope object", NULL, "%s", e.what());
      filterx_object_unref(&self->super.super);
      return NULL;
    }

  return &self->super.super;
}

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  FilterXOtelScope *self = (FilterXOtelScope *) s;

  try
    {
      std::string cstring = self->cpp->repr();
      g_string_assign(repr, cstring.c_str());
    }
  catch (const std::runtime_error &e)
    {
      filterx_eval_push_error_info_printf("Failed to call repr() on OTel Scope object", NULL, "%s", e.what());
      return FALSE;
    }

  return TRUE;
}

FILTERX_SIMPLE_FUNCTION(otel_scope, filterx_otel_scope_new_from_args);

FILTERX_DEFINE_TYPE(otel_scope, FILTERX_TYPE_NAME(mapping),
                    .is_mutable = TRUE,
                    .marshal = _marshal,
                    .clone = _filterx_otel_scope_clone,
                    .truthy = _truthy,
                    .get_subscript = _get_subscript,
                    .set_subscript = _set_subscript,
                    .is_key_set = _is_key_set,
                    .unset_key = _unset_key,
                    .len = _len,
                    .iter = _iter,
                    .repr = _repr,
                    .free_fn = _free,
                   );

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

#ifndef OBJECT_OTEL_ARRAY_HPP
#define OBJECT_OTEL_ARRAY_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "filterx/object-list-interface.h"
#include "object-otel.h"
#include "compat/cpp-end.h"

#include "filterx/protobuf-field-converter.hpp"
#include "object-otel-base.hpp"
#include "opentelemetry/proto/common/v1/common.pb.h"

typedef struct FilterXOtelArray_ FilterXOtelArray;

FilterXObject *_filterx_otel_array_clone(FilterXObject *s);

namespace syslogng {
namespace grpc {
namespace otel {
namespace filterx {

using opentelemetry::proto::common::v1::ArrayValue;

class Array : public Base
{
public:
  Array(FilterXOtelArray *s);
  Array(FilterXOtelArray *s, ArrayValue *a);
  Array(FilterXOtelArray *s, FilterXObject *protobuf_object);
  Array(Array &o) = delete;
  Array(Array &&o) = delete;
  ~Array();

  std::string marshal();
  bool set_subscript(guint64 index, FilterXObject **value);
  FilterXObject *get_subscript(guint64 index);
  bool append(FilterXObject **value);
  bool unset_index(guint64 index);
  guint64 len() const;
  const ArrayValue &get_value() const;

private:
  Array(const Array &o, FilterXOtelArray *s);
  friend FilterXObject *::_filterx_otel_array_clone(FilterXObject *s);

private:
  FilterXOtelArray *super;
  ArrayValue *array;
  bool borrowed;

  friend class ArrayFieldConverter;

protected:
  const google::protobuf::Message &get_protobuf_value() const override;
};

class ArrayFieldConverter : public ProtobufFieldConverter
{
public:
  FilterXObject *get(google::protobuf::Message *message, ProtoReflectors reflectors);
  bool set(google::protobuf::Message *message, ProtoReflectors reflectors, FilterXObject *object,
           FilterXObject **assoc_object);
  bool add(google::protobuf::Message *message, ProtoReflectors reflectors, FilterXObject *object);
};

extern ArrayFieldConverter array_field_converter;

}
}
}
}

#include "compat/cpp-start.h"

struct FilterXOtelArray_
{
  FilterXList super;
  syslogng::grpc::otel::filterx::Array *cpp;
};

#include "compat/cpp-end.h"

#endif

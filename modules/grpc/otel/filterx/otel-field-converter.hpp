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

#ifndef OTEL_FIELD_HPP
#define OTEL_FIELD_HPP

#include "syslog-ng.h"
#include "filterx/protobuf-field-converter.hpp"

#include "compat/cpp-start.h"
#include "filterx/filterx-mapping.h"
#include "compat/cpp-end.h"

#include "opentelemetry/proto/logs/v1/logs.pb.h"

namespace syslogng {
namespace grpc {
namespace otel {

using namespace google::protobuf;
using opentelemetry::proto::logs::v1::LogRecord;

class AnyValueFieldConverter : public ProtobufFieldConverter
{
  using AnyValue = opentelemetry::proto::common::v1::AnyValue;

public:
  FilterXObject *get(Message *message, ProtoReflectors reflectors);
  bool set(Message *message, ProtoReflectors reflectors, FilterXObject *object, FilterXObject **assoc_object);
  bool add(Message *message, ProtoReflectors reflectors, FilterXObject *object);

  FilterXObject *direct_get(AnyValue *any_value);
  bool direct_set(AnyValue *any_value, FilterXObject *object, FilterXObject **assoc_object);
};

extern AnyValueFieldConverter any_value_field;

ProtobufFieldConverter *get_otel_protobuf_field_converter(FieldDescriptor::Type field_type);
ProtobufFieldConverter *get_otel_protobuf_field_converter(const FieldDescriptor *fd);

bool iter_on_otel_protobuf_message_fields(google::protobuf::Message &message, FilterXObjectIterFunc func,
                                          void *user_data);

}
}
}

#endif

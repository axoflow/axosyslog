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

#ifndef OBJECT_OTEL_KVLIST_HPP
#define OBJECT_OTEL_KVLIST_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "filterx/filterx-mapping.h"
#include "object-otel.h"
#include "compat/cpp-end.h"

#include "filterx/protobuf-field-converter.hpp"
#include "object-otel-base.hpp"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include <mutex>

typedef struct FilterXOtelKVList_ FilterXOtelKVList;

FilterXObject *_filterx_otel_kvlist_clone(FilterXObject *s);

namespace syslogng {
namespace grpc {
namespace otel {
namespace filterx {

using opentelemetry::proto::common::v1::KeyValue;
using opentelemetry::proto::common::v1::KeyValueList;
using google::protobuf::RepeatedPtrField;

class KVList : public Base
{

public:
  KVList(FilterXOtelKVList *s);
  KVList(FilterXOtelKVList *s, RepeatedPtrField<KeyValue > *k);
  KVList(FilterXOtelKVList *s, FilterXObject *protobuf_object);
  KVList(KVList &o) = delete;
  KVList(KVList &&o) = delete;
  ~KVList();

  std::string marshal();
  bool set_subscript(FilterXObject *key, FilterXObject **value);
  bool is_key_set(FilterXObject *key) const;
  FilterXObject *get_subscript(FilterXObject *key);
  bool unset_key(FilterXObject *key);
  uint64_t len() const;
  bool iter(FilterXObjectIterFunc func, gpointer user_data) const;
  const RepeatedPtrField<KeyValue> &get_value() const;

private:
  KVList(const KVList &o, FilterXOtelKVList *s);
  friend FilterXObject *::_filterx_otel_kvlist_clone(FilterXObject *s);
  KeyValue *get_mutable_kv_for_key(const std::string &key) const;

private:
  FilterXOtelKVList *super;
  RepeatedPtrField<KeyValue> *repeated_kv;
  bool borrowed;
  thread_local static KeyValueList cached_value;

  friend class KVListFieldConverter;

protected:
  const google::protobuf::Message &get_protobuf_value() const override;
};

class KVListFieldConverter : public ProtobufFieldConverter
{
public:
  FilterXObject *get(google::protobuf::Message *message, ProtoReflectors reflectors);
  bool set(google::protobuf::Message *message, ProtoReflectors reflectors, FilterXObject *object,
           FilterXObject **assoc_object);
  bool add(google::protobuf::Message *message, ProtoReflectors reflectors, FilterXObject *object);
};

extern KVListFieldConverter kvlist_field_converter;

}
}
}
}

#include "compat/cpp-start.h"

struct FilterXOtelKVList_
{
  FilterXMapping super;
  syslogng::grpc::otel::filterx::KVList *cpp;
};

#include "compat/cpp-end.h"

#endif

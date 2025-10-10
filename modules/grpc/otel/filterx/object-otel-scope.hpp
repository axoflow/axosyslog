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

#ifndef OBJECT_OTEL_SCOPE_HPP
#define OBJECT_OTEL_SCOPE_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "filterx/object-dict-interface.h"
#include "object-otel.h"
#include "compat/cpp-end.h"

#include "object-otel-base.hpp"
#include "opentelemetry/proto/common/v1/common.pb.h"

typedef struct FilterXOtelScope_ FilterXOtelScope;

FilterXObject *_filterx_otel_scope_clone(FilterXObject *s);

namespace syslogng {
namespace grpc {
namespace otel {
namespace filterx {

class Scope : public Base
{
public:
  Scope(FilterXOtelScope *s);
  Scope(FilterXOtelScope *s, FilterXObject *protobuf_object);
  Scope(Scope &o) = delete;
  Scope(Scope &&o) = delete;

  std::string marshal();
  bool set_subscript(FilterXObject *key, FilterXObject **value);
  FilterXObject *get_subscript(FilterXObject *key);
  bool unset_key(FilterXObject *key);
  bool is_key_set(FilterXObject *key);
  uint64_t len() const;
  bool iter(FilterXObjectIterFunc func, void *user_data);
  const opentelemetry::proto::common::v1::InstrumentationScope &get_value() const;

private:
  Scope(const Scope &o, FilterXOtelScope *s);
  friend FilterXObject *::_filterx_otel_scope_clone(FilterXObject *s);

private:
  FilterXOtelScope *super;
  opentelemetry::proto::common::v1::InstrumentationScope scope;

protected:
  const google::protobuf::Message &get_protobuf_value() const override;
};

}
}
}
}

#include "compat/cpp-start.h"

struct FilterXOtelScope_
{
  FilterXDict super;
  syslogng::grpc::otel::filterx::Scope *cpp;
};

#include "compat/cpp-end.h"

#endif

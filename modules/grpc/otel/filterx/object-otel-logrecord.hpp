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

#ifndef OBJECT_OTEL_LOGRECORD_HPP
#define OBJECT_OTEL_LOGRECORD_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "filterx/filterx-mapping.h"
#include "object-otel.h"
#include "compat/cpp-end.h"

#include "object-otel-base.hpp"
#include "opentelemetry/proto/logs/v1/logs.pb.h"

typedef struct FilterXOtelLogRecord_ FilterXOtelLogRecord;

FilterXObject *_filterx_otel_logrecord_clone(FilterXObject *s);

namespace syslogng {
namespace grpc {
namespace otel {
namespace filterx {

class LogRecord : public Base
{
public:
  LogRecord(FilterXOtelLogRecord *super);
  LogRecord(FilterXOtelLogRecord *super, FilterXObject *protobuf_object);
  LogRecord(LogRecord &o) = delete;
  LogRecord(LogRecord &&o) = delete;
  std::string marshal(void);
  FilterXObject *get_subscript(FilterXObject *key);
  bool set_subscript(FilterXObject *key, FilterXObject **value);
  bool unset_key(FilterXObject *key);
  bool is_key_set(FilterXObject *key);
  uint64_t len() const;
  bool iter(FilterXObjectIterFunc func, void *user_data);
  const opentelemetry::proto::logs::v1::LogRecord &get_value() const;
private:
  FilterXOtelLogRecord *super;
  opentelemetry::proto::logs::v1::LogRecord logRecord;
  LogRecord(const LogRecord &o, FilterXOtelLogRecord *super);
  friend FilterXObject *::_filterx_otel_logrecord_clone(FilterXObject *s);

protected:
  const google::protobuf::Message &get_protobuf_value() const override;
};

}
}
}
}

#include "compat/cpp-start.h"

struct FilterXOtelLogRecord_
{
  FilterXDict super;
  syslogng::grpc::otel::filterx::LogRecord *cpp;
};

#include "compat/cpp-end.h"

#endif

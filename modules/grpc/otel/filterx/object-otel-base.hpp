/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter
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

#ifndef OBJECT_OTEL_BASE_HPP
#define OBJECT_OTEL_BASE_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "filterx/filterx-sequence.h"
#include "object-otel.h"
#include "compat/cpp-end.h"

#include "opentelemetry/proto/common/v1/common.pb.h"

namespace syslogng {
namespace grpc {
namespace otel {
namespace filterx {

class Base
{
public:
  virtual ~Base() = default;
  std::string repr() const;
protected:
  virtual const google::protobuf::Message &get_protobuf_value() const = 0;
};

}
}
}
}

#endif

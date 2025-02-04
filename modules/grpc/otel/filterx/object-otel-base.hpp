/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include "filterx/object-list-interface.h"
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

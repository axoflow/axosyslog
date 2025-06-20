/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 shifter
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef FILTERX_PROTOBUF_FORMATTER_HPP
#define FILTERX_PROTOBUF_FORMATTER_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "filterx/filterx-object.h"
#include "compat/cpp-end.h"

#include "schema/proto-schema-provider.hpp"

#include <grpc++/grpc++.h>
#include <string>

namespace syslogng {
namespace grpc {

class FilterXProtobufFormatter
{
public:
  FilterXProtobufFormatter(const std::string &proto_file_path);

  google::protobuf::Message *format(FilterXObject *object) const;

private:
  ProtoSchemaFileLoader proto_schema_file_loader;
};

}
}

#endif

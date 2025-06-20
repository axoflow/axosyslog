/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
 * Copyright (c) 2023 László Várady
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

#ifndef PROTO_SCHEMA_PROVIDER_HPP
#define PROTO_SCHEMA_PROVIDER_HPP

#include "syslog-ng.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>
#include <google/protobuf/compiler/importer.h>

namespace syslogng {
namespace grpc {

class ProtoSchemaProvider
{
public:
  virtual bool init() = 0;

  const google::protobuf::Message &get_schema_prototype() const
  {
    return *this->schema_prototype;
  }

  const google::protobuf::Descriptor &get_schema_descriptor() const
  {
    return *this->schema_descriptor;
  }

protected:
  const google::protobuf::Descriptor *schema_descriptor = nullptr;
  const google::protobuf::Message *schema_prototype = nullptr;
};

class ProtoSchemaFileLoader : public ProtoSchemaProvider
{
public:
  ProtoSchemaFileLoader() {};

  bool init();

  void set_proto_file_path(const std::string &proto_file_path_)
  {
    this->proto_file_path = proto_file_path_;
  }

private:
  bool loaded = false;
  std::string proto_file_path;
  std::unique_ptr<google::protobuf::compiler::Importer> importer = nullptr;

  /* A given descriptor_pool/importer instance should outlive msg_factory, as msg_factory caches prototypes */
  std::unique_ptr<google::protobuf::DynamicMessageFactory> msg_factory = nullptr;

  std::unique_ptr<google::protobuf::compiler::DiskSourceTree> src_tree = nullptr;
  std::unique_ptr<google::protobuf::compiler::MultiFileErrorCollector> error_coll = nullptr;
};

class ProtoSchemaBuilder: public ProtoSchemaProvider
{
public:
  using MapTypeFn =
    std::function<bool (const std::string &type_in, google::protobuf::FieldDescriptorProto::Type &type_out)>;

public:
  ProtoSchemaBuilder(MapTypeFn map_type, int proto_version, const std::string &file_descriptor_proto_name,
                     const std::string &descriptor_proto_name);

  bool init();

  bool add_field(const std::string &name, const std::string &type);

private:
  MapTypeFn map_type;
  google::protobuf::DescriptorPool descriptor_pool;

  /* A given descriptor_pool/importer instance should outlive msg_factory, as msg_factory caches prototypes */
  std::unique_ptr<google::protobuf::DynamicMessageFactory> msg_factory = nullptr;

  google::protobuf::FileDescriptorProto file_descriptor_proto;
  google::protobuf::DescriptorProto *descriptor_proto = nullptr;

  int field_id = 1;
};

}
}

#endif

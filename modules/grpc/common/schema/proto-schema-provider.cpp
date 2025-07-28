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

#include "proto-schema-provider.hpp"

#include "compat/cpp-start.h"
#include "messages.h"
#include "compat/cpp-end.h"

#include <absl/strings/string_view.h>

using namespace syslogng::grpc;

namespace {
class ErrorCollector : public google::protobuf::compiler::MultiFileErrorCollector
{
public:
  ErrorCollector() {}
  ~ErrorCollector() override {}

  // override is missing for compatibility with older protobuf versions
  void RecordError(absl::string_view filename, int line, int column, absl::string_view message)
  {
    std::string file{filename};
    std::string msg{message};

    msg_error("Error parsing protobuf-schema() file",
              evt_tag_str("filename", file.c_str()), evt_tag_int("line", line), evt_tag_int("column", column),
              evt_tag_str("error", msg.c_str()));
  }

  // override is missing for compatibility with older protobuf versions
  void RecordWarning(absl::string_view filename, int line, int column, absl::string_view message)
  {
    std::string file{filename};
    std::string msg{message};

    msg_error("Warning during parsing protobuf-schema() file",
              evt_tag_str("filename", file.c_str()), evt_tag_int("line", line), evt_tag_int("column", column),
              evt_tag_str("warning", msg.c_str()));
  }

private:
  /* deprecated interface */
  void AddError(const std::string &filename, int line, int column, const std::string &message)
  {
    this->RecordError(filename, line, column, message);
  }

  void AddWarning(const std::string &filename, int line, int column, const std::string &message)
  {
    this->RecordWarning(filename, line, column, message);
  }
};
}

bool
ProtoSchemaFileLoader::init()
{
  if (this->loaded)
    return true;

  this->msg_factory = std::make_unique<google::protobuf::DynamicMessageFactory>();
  this->importer.reset(nullptr);

  this->src_tree = std::make_unique<google::protobuf::compiler::DiskSourceTree>();
  this->src_tree->MapPath(this->proto_file_path, this->proto_file_path);

  this->error_coll = std::make_unique<ErrorCollector>();

  this->importer = std::make_unique<google::protobuf::compiler::Importer>(this->src_tree.get(), this->error_coll.get());

  const google::protobuf::FileDescriptor *file_descriptor = this->importer->Import(this->proto_file_path);

  if (!file_descriptor || file_descriptor->message_type_count() == 0)
    {
      msg_error("Error initializing gRPC based destination, protobuf-schema() file can't be loaded");
      return false;
    }

  this->schema_descriptor = file_descriptor->message_type(0);
  this->schema_prototype = this->msg_factory->GetPrototype(this->schema_descriptor);
  this->loaded = true;

  return true;
}

ProtoSchemaBuilder::ProtoSchemaBuilder(MapTypeFn map_type_, int proto_version,
                                       const std::string &file_descriptor_proto_name,
                                       const std::string &descriptor_proto_name) :
  map_type(map_type_)
{
  this->msg_factory = std::make_unique<google::protobuf::DynamicMessageFactory>();

  this->file_descriptor_proto.set_name(file_descriptor_proto_name);
  this->file_descriptor_proto.set_syntax(proto_version == 2 ? "proto2" : "proto3");

  this->descriptor_proto = this->file_descriptor_proto.add_message_type();
  this->descriptor_proto->set_name(descriptor_proto_name);
}

bool
ProtoSchemaBuilder::init()
{
  const google::protobuf::FileDescriptor *file_descriptor = this->descriptor_pool.BuildFile(this->file_descriptor_proto);
  if (!file_descriptor || file_descriptor->message_type_count() == 0)
    {
      msg_error("Error initializing gRPC based destination, protobuf-schema() file cannot be built");
      return false;
    }

  this->schema_descriptor = file_descriptor->message_type(0);
  this->schema_prototype = this->msg_factory->GetPrototype(this->schema_descriptor);

  return true;
}

bool
ProtoSchemaBuilder::add_field(const std::string &name, const std::string &type)
{
  google::protobuf::FieldDescriptorProto::Type proto_type;

  if (!this->map_type(type, proto_type))
    return false;

  google::protobuf::FieldDescriptorProto *field_desc_proto = this->descriptor_proto->add_field();
  field_desc_proto->set_name(name);
  field_desc_proto->set_type(proto_type);
  field_desc_proto->set_number(this->field_id++);

  return true;
}

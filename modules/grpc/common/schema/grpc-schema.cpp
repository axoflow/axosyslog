/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "grpc-schema.hpp"

#include "compat/cpp-start.h"
#include "scratch-buffers.h"
#include "compat/cpp-end.h"

#include <absl/strings/string_view.h>

using namespace syslogng::grpc;

static void
_template_unref(gpointer data)
{
  LogTemplate *tpl = (LogTemplate *) data;
  log_template_unref(tpl);
}

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

LogMessageProtobufFormatter::LogMessageProtobufFormatter(std::unique_ptr<ProtoSchemaBuilder> schema_builder_,
                                                         LogTemplateOptions *template_options_,
                                                         LogPipe *log_pipe_) :
  template_options(template_options_),
  log_pipe(log_pipe_),
  protobuf_schema{nullptr, std::move(schema_builder_)}
{
}

bool
LogMessageProtobufFormatter::init()
{
  if (!this->protobuf_schema.provider)
    {
      msg_error("Error initializing gRPC based destination, schema() or protobuf-schema() must be set",
                log_pipe_location_tag(this->log_pipe));
      return false;
    }

  if (!this->protobuf_schema.provider->init())
    return false;

  const google::protobuf::Descriptor &schema_descriptor = this->protobuf_schema.provider->get_schema_descriptor();
  size_t field_count = (size_t) schema_descriptor.field_count();

  if (this->fields.size() != field_count)
    {
      msg_error("Error initializing gRPC based destination, protobuf schema has different number of fields than "
                "values listed in the config",
                log_pipe_location_tag(this->log_pipe));
      return false;
    }

  for (size_t i = 0; i < field_count; i++)
    {
      Field &field = this->fields[i];
      field.field_desc = schema_descriptor.field(i);
      field.nv.name = field.field_desc->name();
    }

  return true;
}

bool
LogMessageProtobufFormatter::add_field(std::string name, std::string type, LogTemplate *value)
{
  this->protobuf_schema.provider = this->protobuf_schema.builder.get();

  if (!this->protobuf_schema.builder->add_field(name, type))
    return false;

  this->fields.push_back(Field(value));
  return true;
}

void
LogMessageProtobufFormatter::set_protobuf_schema(std::string proto_path, GList *values)
{
  this->protobuf_schema.provider = &this->protobuf_schema.file_loader;
  this->protobuf_schema.file_loader.set_proto_file_path(proto_path);

  for (GList *current_value = values; current_value; current_value = current_value->next)
    {
      LogTemplate *value = (LogTemplate *) current_value->data;
      this->fields.push_back(Field(value));
    }

  g_list_free_full(values, _template_unref);
}

google::protobuf::Message *
LogMessageProtobufFormatter::format(LogMessage *msg, gint seq_num) const
{
  google::protobuf::Message *message = this->protobuf_schema.provider->get_schema_prototype().New();
  const google::protobuf::Reflection *reflection = message->GetReflection();

  bool msg_has_field = false;
  for (const auto &field : fields)
    {
      bool field_inserted = this->insert_field(reflection, field, seq_num, msg, message);
      msg_has_field |= field_inserted;

      if (!field_inserted && (this->template_options->on_error & ON_ERROR_DROP_MESSAGE))
        goto drop;
    }

  if (!msg_has_field)
    goto drop;

  return message;

drop:
  delete message;
  return nullptr;
}

LogMessageProtobufFormatter::Slice
LogMessageProtobufFormatter::format_template(LogTemplate *tmpl, LogMessage *msg, GString *value,
                                             LogMessageValueType *type,
                                             gint seq_num) const
{
  if (log_template_is_trivial(tmpl))
    {
      gssize trivial_value_len;
      const gchar *trivial_value = log_template_get_trivial_value_and_type(tmpl, msg, &trivial_value_len, type);

      if (trivial_value_len < 0)
        return Slice{"", 0};

      return Slice{trivial_value, (std::size_t) trivial_value_len};
    }

  LogTemplateEvalOptions options = {this->template_options, LTZ_SEND, seq_num, NULL, LM_VT_STRING};
  log_template_format_value_and_type(tmpl, msg, &options, value, type);
  return Slice{value->str, value->len};
}

bool
LogMessageProtobufFormatter::insert_field(const google::protobuf::Reflection *reflection, const Field &field,
                                          gint seq_num,
                                          LogMessage *msg, google::protobuf::Message *message) const
{
  ScratchBuffersMarker m;
  GString *buf = scratch_buffers_alloc_and_mark(&m);

  LogMessageValueType type;

  Slice value = this->format_template(field.nv.value, msg, buf, &type, seq_num);

  if (type == LM_VT_NULL)
    {
      if (field.field_desc->is_required())
        {
          msg_error("Missing required field", evt_tag_str("field", field.nv.name.c_str()));
          goto error;
        }

      scratch_buffers_reclaim_marked(m);
      return true;
    }

  switch (field.field_desc->cpp_type())
    {
    /* TYPE_STRING, TYPE_BYTES (embedded nulls are possible, no null-termination is assumed) */
    case google::protobuf::FieldDescriptor::CppType::CPPTYPE_STRING:
      reflection->SetString(message, field.field_desc, std::string{value.str, value.len});
      break;
    case google::protobuf::FieldDescriptor::CppType::CPPTYPE_INT32:
    {
      int32_t v;
      if (!type_cast_to_int32(value.str, -1, &v, NULL))
        {
          type_cast_drop_helper(this->template_options->on_error, value.str, -1, "integer");
          goto error;
        }
      reflection->SetInt32(message, field.field_desc, v);
      break;
    }
    case google::protobuf::FieldDescriptor::CppType::CPPTYPE_INT64:
    {
      gint64 v;
      if (!type_cast_to_int64(value.str, -1, &v, NULL))
        {
          type_cast_drop_helper(this->template_options->on_error, value.str, -1, "integer");
          goto error;
        }
      reflection->SetInt64(message, field.field_desc, v);
      break;
    }
    case google::protobuf::FieldDescriptor::CppType::CPPTYPE_UINT32:
    {
      gint64 v;
      if (!type_cast_to_int64(value.str, -1, &v, NULL))
        {
          type_cast_drop_helper(this->template_options->on_error, value.str, -1, "integer");
          goto error;
        }
      reflection->SetUInt32(message, field.field_desc, (uint32_t) v);
      break;
    }
    case google::protobuf::FieldDescriptor::CppType::CPPTYPE_UINT64:
    {
      gint64 v;
      if (!type_cast_to_int64(value.str, -1, &v, NULL))
        {
          type_cast_drop_helper(this->template_options->on_error, value.str, -1, "integer");
          goto error;
        }
      reflection->SetUInt64(message, field.field_desc, (uint64_t) v);
      break;
    }
    case google::protobuf::FieldDescriptor::CppType::CPPTYPE_DOUBLE:
    {
      double v;
      if (!type_cast_to_double(value.str, -1, &v, NULL))
        {
          type_cast_drop_helper(this->template_options->on_error, value.str, -1, "double");
          goto error;
        }
      reflection->SetDouble(message, field.field_desc, v);
      break;
    }
    case google::protobuf::FieldDescriptor::CppType::CPPTYPE_FLOAT:
    {
      double v;
      if (!type_cast_to_double(value.str, -1, &v, NULL))
        {
          type_cast_drop_helper(this->template_options->on_error, value.str, -1, "double");
          goto error;
        }
      reflection->SetFloat(message, field.field_desc, (float) v);
      break;
    }
    case google::protobuf::FieldDescriptor::CppType::CPPTYPE_BOOL:
    {
      gboolean v;
      if (!type_cast_to_boolean(value.str, -1, &v, NULL))
        {
          type_cast_drop_helper(this->template_options->on_error, value.str, -1, "boolean");
          goto error;
        }
      reflection->SetBool(message, field.field_desc, v);
      break;
    }
    default:
      goto error;
    }

  scratch_buffers_reclaim_marked(m);
  return true;

error:
  scratch_buffers_reclaim_marked(m);
  return false;
}

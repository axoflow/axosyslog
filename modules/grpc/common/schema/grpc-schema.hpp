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

#ifndef GRPC_SCHEMA_HPP
#define GRPC_SCHEMA_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "template/templates.h"
#include "logpipe.h"
#include "compat/cpp-end.h"

#include "proto-schema-provider.hpp"

#include <grpc++/grpc++.h>

#include <memory>
#include <vector>

namespace syslogng {
namespace grpc {

struct NameValueTemplatePair
{
  std::string name;
  LogTemplate *value;

  NameValueTemplatePair(std::string name_, LogTemplate *value_)
    : name(name_), value(log_template_ref(value_)) {}

  NameValueTemplatePair(const NameValueTemplatePair &a)
    : name(a.name), value(log_template_ref(a.value)) {}

  NameValueTemplatePair &operator=(const NameValueTemplatePair &a)
  {
    name = a.name;
    log_template_unref(value);
    value = log_template_ref(a.value);

    return *this;
  }

  ~NameValueTemplatePair()
  {
    log_template_unref(value);
  }

};

struct Field
{
  NameValueTemplatePair nv;
  const google::protobuf::FieldDescriptor *field_desc;

  Field(LogTemplate *value_)
    : nv("", value_) {}

  Field(const Field &a)
    : nv(a.nv), field_desc(a.field_desc) {}

  Field &operator=(const Field &a)
  {
    nv = a.nv;
    field_desc = a.field_desc;

    return *this;
  }

};

class LogMessageProtobufFormatter
{
private:
  struct Slice
  {
    const char *str;
    std::size_t len;
  };

public:
  LogMessageProtobufFormatter(std::unique_ptr<ProtoSchemaBuilder> schema_builder,
                              LogTemplateOptions *template_options, LogPipe *log_pipe);

  bool init();
  google::protobuf::Message *format(LogMessage *msg, gint seq_num) const;

  bool empty() const
  {
    return this->fields.empty();
  }

  const google::protobuf::Descriptor &get_schema_descriptor() const
  {
    return this->protobuf_schema.provider->get_schema_descriptor();
  }

  /* For grammar. */
  bool add_field(std::string name, std::string type, LogTemplate *value);
  void set_protobuf_schema(std::string proto_path, GList *values);

private:
  Slice format_template(LogTemplate *tmpl, LogMessage *msg, GString *value, LogMessageValueType *type,
                        gint seq_num) const;
  bool insert_field(const google::protobuf::Reflection *reflection, const Field &field, gint seq_num,
                    LogMessage *msg, google::protobuf::Message *message) const;

private:
  LogTemplateOptions *template_options;
  LogPipe *log_pipe;

  struct
  {
    ProtoSchemaProvider *provider;
    std::unique_ptr<ProtoSchemaBuilder> builder;
    ProtoSchemaFileLoader file_loader;
  } protobuf_schema;

  std::vector<Field> fields;
};

}
}

#endif

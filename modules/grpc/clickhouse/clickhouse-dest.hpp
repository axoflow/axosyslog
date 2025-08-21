/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef CLICKHOUSE_DEST_HPP
#define CLICKHOUSE_DEST_HPP

#include "clickhouse-dest.h"
#include "grpc-dest.hpp"

#include <string>
#include <google/protobuf/util/json_util.h>

namespace syslogng {
namespace grpc {
namespace clickhouse {

class DestDriver final : public syslogng::grpc::DestDriver
{
public:
  DestDriver(GrpcDestDriver *s);
  bool init();
  const gchar *generate_persist_name();
  const gchar *format_stats_key(StatsClusterKeyBuilder *kb);
  LogThreadedDestWorker *construct_worker(int worker_index);

  void set_database(const std::string &d)
  {
    this->database = d;
  }

  void set_table(const std::string &t)
  {
    this->table = t;
  }

  void set_user(const std::string &u)
  {
    this->user = u;
  }

  void set_password(const std::string &p)
  {
    this->password = p;
  }

  void set_server_side_schema(const std::string &s)
  {
    this->server_side_schema = s;
  }

  void set_json_var(LogTemplate *json_var_)
  {
    log_template_unref(this->json_var);
    this->json_var = log_template_ref(json_var_);
  }

  const char *format_json_var(LogMessage *log_msg, ssize_t *len)
  {
    if (!json_mode())
      return nullptr;

    LogMessageValueType lmvt;
    const gchar *serialized = log_template_get_trivial_value_and_type(this->json_var, log_msg, len, &lmvt);
    if (lmvt != LM_VT_JSON)
      {
        msg_error("Error LogMessage type is not string",
                  evt_tag_int("expected_type", LM_VT_JSON),
                  evt_tag_int("current_type", lmvt));
        return nullptr;
      }
    return serialized;
  }

  const std::string &get_database()
  {
    return this->database;
  }

  const std::string &get_table()
  {
    return this->table;
  }

  const std::string &get_user()
  {
    return this->user;
  }

  const std::string &get_password()
  {
    return this->password;
  }

  const std::string &get_query()
  {
    return this->query;
  }

  LogMessageProtobufFormatter *get_log_message_protobuf_formatter()
  {
    return &this->log_message_protobuf_formatter;
  }

  bool json_mode(void)
  {
    return this->json_var != nullptr;
  }

private:
  static bool map_schema_type(const std::string &type_in, google::protobuf::FieldDescriptorProto::Type &type_out);
  bool quote_identifier(const std::string &identifier, std::string &quoted_identifier);
  bool check_schema_options();

private:
  friend class DestWorker;

private:
  std::string database;
  std::string table;
  std::string user;
  std::string password;
  std::string server_side_schema;
  std::string query;
  LogTemplate *json_var = nullptr;

  LogMessageProtobufFormatter log_message_protobuf_formatter;
};


}
}
}

syslogng::grpc::clickhouse::DestDriver *clickhouse_dd_get_cpp(GrpcDestDriver *self);

#endif

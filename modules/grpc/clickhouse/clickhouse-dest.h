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

#ifndef CLICKHOUSE_DEST_H
#define CLICKHOUSE_DEST_H

typedef enum
{
  JSONEACHROW,
  JSONCOMPACTEACHROW,
  PROTOBUF
} format_t;

#define CLICKHOUSE_DESTINATION_FORMAT_JSONEACHROW "JSONEachRow"
#define CLICKHOUSE_DESTINATION_FORMAT_JSONCOMPACTEACHROW "JSONCompactEachRow"
#define CLICKHOUSE_DESTINATION_FORMAT_PROTOBUF "Protobuf"
#define CLICKHOUSE_DESTINATION_LIST_FORMATS \
    CLICKHOUSE_DESTINATION_FORMAT_JSONEACHROW ", " \
    CLICKHOUSE_DESTINATION_FORMAT_JSONCOMPACTEACHROW ", " \
    CLICKHOUSE_DESTINATION_FORMAT_PROTOBUF

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "driver.h"
#include "template/templates.h"

LogDriver *clickhouse_dd_new(GlobalConfig *cfg);

void clickhouse_dd_set_database(LogDriver *d, const gchar *database);
void clickhouse_dd_set_table(LogDriver *d, const gchar *table);
void clickhouse_dd_set_user(LogDriver *d, const gchar *user);
void clickhouse_dd_set_password(LogDriver *d, const gchar *password);
void clickhouse_dd_set_server_side_schema(LogDriver *d, const gchar *server_side_schema);
void clickhouse_dd_set_json_var(LogDriver *d, LogTemplate *json_var);
gboolean clickhouse_dd_set_format(LogDriver *d, const gchar *format);

#include "compat/cpp-end.h"

#endif

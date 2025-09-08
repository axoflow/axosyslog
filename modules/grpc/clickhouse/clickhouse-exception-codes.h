/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter <shifter@axoflow.com>
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

#include <stddef.h>
#include "logthrdest/logthrdestdrv.h"
#include "compat/cpp-start.h"

typedef enum
{
  ACTION_RETRY_FOREVER,
  ACTION_RETRY_THEN_DROP,
  ACTION_DROP
} ErrorAction;


// The full list of ClickHouse exception codes is defined here:
// https://github.com/ClickHouse/ClickHouse/blob/master/src/Common/ErrorCodes.cpp

#define CLICKHOUSE_ERROR_CODE_LAST 1004
// The ClickHouseErrorCode enum contains only the subset of error codes currently in use,
// to simplify maintenance. Any codes not included are handled via the fallback path (default: ACTION_RETRY_LIMITED).
// The highest known code at the time of writing was CLICKHOUSE_ERROR_CODE_LAST.
// When updating this enum, be sure to account for any new codes added beyond CLICKHOUSE_ERROR_CODE_LAST.

typedef enum ClickHouseErrorCode
{
  UNSUPPORTED_METHOD = 1,
  UNSUPPORTED_PARAMETER = 2,
  CANNOT_PARSE_TEXT = 6,
  INCORRECT_NUMBER_OF_COLUMNS = 7,
  SIZE_OF_FIXED_STRING_DOESNT_MATCH = 19,
  NUMBER_OF_COLUMNS_DOESNT_MATCH = 20,
  CANNOT_PARSE_ESCAPE_SEQUENCE = 25,
  CANNOT_PARSE_QUOTED_STRING = 26,
  BAD_ARGUMENTS = 36,
  CANNOT_PARSE_DATE = 38,
  CANNOT_PARSE_DATETIME = 41,
  UNKNOWN_FUNCTION = 46,
  UNKNOWN_IDENTIFIER = 47,
  LOGICAL_ERROR = 49,
  TYPE_MISMATCH = 53,
  UNKNOWN_TABLE = 60,
  SYNTAX_ERROR = 62,
  CANNOT_PARSE_NUMBER = 72,
  UNKNOWN_FORMAT = 73,
  INCORRECT_QUERY = 80,
  UNKNOWN_DATABASE = 81,
  CANNOT_READ_FROM_SOCKET = 95,
  CANNOT_WRITE_TO_SOCKET = 96,
  NO_DATA_TO_INSERT = 108,
  INCORRECT_DATA = 117,
  TIMEOUT_EXCEEDED = 159,
  TOO_SLOW = 160,
  TOO_MANY_SIMULTANEOUS_QUERIES = 202,
  NO_FREE_CONNECTION = 203,
  SOCKET_TIMEOUT = 209,
  NETWORK_ERROR = 210,
  QUERY_WITH_SAME_ID_IS_ALREADY_RUNNING = 216,
  REPLICA_IS_ALREADY_ACTIVE = 224,
  NO_ZOOKEEPER = 225,
  BAD_DATA_PART_NAME = 233,
  UNEXPECTED_ZOOKEEPER_ERROR = 244,
  CORRUPTED_DATA = 246,
  NO_ACTIVE_REPLICAS = 254,
  NO_AVAILABLE_REPLICA = 265,
  ALL_CONNECTION_TRIES_FAILED = 279,
  TOO_FEW_LIVE_REPLICAS = 285,
  UNSATISFIED_QUORUM_FOR_PREVIOUS_WRITE = 286,
  UNKNOWN_FORMAT_VERSION = 287,
  REPLICA_IS_NOT_IN_QUORUM = 289,
  UNKNOWN_DATABASE_ENGINE = 336,
  RECEIVED_ERROR_TOO_MANY_REQUESTS = 364,
  ALL_REPLICAS_ARE_STALE = 369,
  DATA_TYPE_CANNOT_BE_USED_IN_TABLES = 370,
  CANNOT_PARSE_UUID = 376,
  PART_IS_TEMPORARILY_LOCKED = 384,
  ALL_REPLICAS_LOST = 415,
  REPLICA_STATUS_CHANGED = 416,
  CANNOT_PARSE_PROTOBUF_SCHEMA = 434,
  NO_COLUMN_SERIALIZED_TO_REQUIRED_PROTOBUF_FIELD = 435,
  PROTOBUF_BAD_CAST = 436,
  NOT_A_LEADER = 529,
  MYSQL_SYNTAX_ERROR = 538,
  DATA_TYPE_CANNOT_BE_USED_IN_KEY = 549,
  MULTIPLE_COLUMNS_SERIALIZED_TO_SAME_PROTOBUF_FIELD = 569,
  DATA_TYPE_INCOMPATIBLE_WITH_PROTOBUF_FIELD = 570,
  DISTRIBUTED_TOO_MANY_PENDING_BYTES = 574,
  ZERO_COPY_REPLICATION_ERROR = 593,
  CANNOT_CONNECT_NATS = 665,
  AWS_ERROR = 693,
  GCP_ERROR = 707,
  TOO_SLOW_PARSING = 718,
  GOOGLE_CLOUD_ERROR = 737,
  POTENTIALLY_BROKEN_DATA_PART = 740,
  SERVER_OVERLOADED = 745,
  KEEPER_EXCEPTION = 999,
} ClickHouseErrorCode;

extern ErrorAction classify_clickhouse_error(ClickHouseErrorCode code);
extern LogThreadedResult error_action_to_logthreaded_result(ErrorAction error_action);

#include "compat/cpp-end.h"

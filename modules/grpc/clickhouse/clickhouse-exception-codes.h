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

typedef enum
{
  ACTION_RETRY_FOREVER,
  ACTION_RETRY_LIMITED,
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
  CANNOT_PARSE_TEXT = 6,
  INCORRECT_NUMBER_OF_COLUMNS = 7,
  SIZE_OF_FIXED_STRING_DOESNT_MATCH = 19,
  NUMBER_OF_COLUMNS_DOESNT_MATCH = 20,
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

// Retry forever (transient infra issues)
static const ClickHouseErrorCode CLICKHOUSE_ERRORS_RETRY_FOREVER[] =
{
  CANNOT_READ_FROM_SOCKET,
  CANNOT_WRITE_TO_SOCKET,
  TIMEOUT_EXCEEDED,
  TOO_SLOW,
  TOO_MANY_SIMULTANEOUS_QUERIES,
  NO_FREE_CONNECTION,
  SOCKET_TIMEOUT,
  NETWORK_ERROR,
  QUERY_WITH_SAME_ID_IS_ALREADY_RUNNING,
  REPLICA_IS_ALREADY_ACTIVE,
  NO_ZOOKEEPER,
  UNEXPECTED_ZOOKEEPER_ERROR,
  NO_ACTIVE_REPLICAS,
  NO_AVAILABLE_REPLICA,
  ALL_CONNECTION_TRIES_FAILED,
  TOO_FEW_LIVE_REPLICAS,
  UNSATISFIED_QUORUM_FOR_PREVIOUS_WRITE,
  REPLICA_IS_NOT_IN_QUORUM,
  RECEIVED_ERROR_TOO_MANY_REQUESTS,
  ALL_REPLICAS_ARE_STALE,
  PART_IS_TEMPORARILY_LOCKED,
  REPLICA_STATUS_CHANGED,
  ALL_REPLICAS_LOST,
  NOT_A_LEADER,
  DISTRIBUTED_TOO_MANY_PENDING_BYTES,
  ZERO_COPY_REPLICATION_ERROR,
  CANNOT_CONNECT_NATS,
  AWS_ERROR,
  GCP_ERROR,
  GOOGLE_CLOUD_ERROR,
  SERVER_OVERLOADED,
  KEEPER_EXCEPTION,
};

// Drop immediately (permanent data/query issues)
static const ClickHouseErrorCode CLICKHOUSE_ERRORS_DROP_IMMEDIATE[] =
{
  BAD_ARGUMENTS,
  SYNTAX_ERROR,
  INCORRECT_QUERY,
  UNKNOWN_FUNCTION,
  UNKNOWN_IDENTIFIER,
  UNKNOWN_TABLE,
  UNKNOWN_DATABASE,
  UNKNOWN_FORMAT,
  CANNOT_PARSE_TEXT,
  CANNOT_PARSE_NUMBER,
  CANNOT_PARSE_DATE,
  CANNOT_PARSE_DATETIME,
  CANNOT_PARSE_UUID,
  INCORRECT_NUMBER_OF_COLUMNS,
  NUMBER_OF_COLUMNS_DOESNT_MATCH,
  TYPE_MISMATCH,
  SIZE_OF_FIXED_STRING_DOESNT_MATCH,
  DATA_TYPE_CANNOT_BE_USED_IN_TABLES,
  DATA_TYPE_CANNOT_BE_USED_IN_KEY,
  CANNOT_PARSE_PROTOBUF_SCHEMA,
  NO_COLUMN_SERIALIZED_TO_REQUIRED_PROTOBUF_FIELD,
  PROTOBUF_BAD_CAST,
  MULTIPLE_COLUMNS_SERIALIZED_TO_SAME_PROTOBUF_FIELD,
  DATA_TYPE_INCOMPATIBLE_WITH_PROTOBUF_FIELD,
  CORRUPTED_DATA,
  BAD_DATA_PART_NAME,
  POTENTIALLY_BROKEN_DATA_PART,
  LOGICAL_ERROR,
};

#define NUM_CLICKHOUSE_ERRORS_RETRY_FOREVER \
    (sizeof(CLICKHOUSE_ERRORS_RETRY_FOREVER) / sizeof(ClickHouseErrorCode))

#define NUM_CLICKHOUSE_ERRORS_DROP_IMMEDIATE \
    (sizeof(CLICKHOUSE_ERRORS_DROP_IMMEDIATE) / sizeof(ClickHouseErrorCode))

static inline bool is_in_set(ClickHouseErrorCode code,
                             const ClickHouseErrorCode *set,
                             size_t set_size)
{
  for (size_t i = 0; i < set_size; i++)
    {
      if (set[i] == code) return true;
    }
  return false;
}

static inline ErrorAction classify_clickhouse_error(ClickHouseErrorCode code)
{
  if (is_in_set(code, CLICKHOUSE_ERRORS_RETRY_FOREVER,
                NUM_CLICKHOUSE_ERRORS_RETRY_FOREVER))
    {
      return ACTION_RETRY_FOREVER;
    }

  if (is_in_set(code, CLICKHOUSE_ERRORS_DROP_IMMEDIATE,
                NUM_CLICKHOUSE_ERRORS_DROP_IMMEDIATE))
    {
      return ACTION_DROP;
    }

  return ACTION_RETRY_LIMITED;
}

static inline LogThreadedResult error_action_to_logthreaded_result(ErrorAction error_action)
{
  switch (error_action)
    {
    case ACTION_RETRY_FOREVER:
      return LTR_NOT_CONNECTED;
      break;
    case ACTION_RETRY_LIMITED:
      return LTR_ERROR;
      break;
    case ACTION_DROP:
      return LTR_DROP;
      break;
    default:
      g_assert_not_reached();
      break;
    }
}

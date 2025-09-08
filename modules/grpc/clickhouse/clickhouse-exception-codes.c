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

#include "clickhouse-exception-codes.h"

// Retry forever (transient infra issues)
static const gboolean CLICKHOUSE_ERRORS_RETRY_FOREVER[] =
{
  [CANNOT_READ_FROM_SOCKET] = TRUE,
  [CANNOT_WRITE_TO_SOCKET] = TRUE,
  [TIMEOUT_EXCEEDED] = TRUE,
  [TOO_SLOW] = TRUE,
  [TOO_MANY_SIMULTANEOUS_QUERIES] = TRUE,
  [NO_FREE_CONNECTION] = TRUE,
  [SOCKET_TIMEOUT] = TRUE,
  [NETWORK_ERROR] = TRUE,
  [QUERY_WITH_SAME_ID_IS_ALREADY_RUNNING] = TRUE,
  [REPLICA_IS_ALREADY_ACTIVE] = TRUE,
  [NO_ZOOKEEPER] = TRUE,
  [UNEXPECTED_ZOOKEEPER_ERROR] = TRUE,
  [NO_ACTIVE_REPLICAS] = TRUE,
  [NO_AVAILABLE_REPLICA] = TRUE,
  [ALL_CONNECTION_TRIES_FAILED] = TRUE,
  [TOO_FEW_LIVE_REPLICAS] = TRUE,
  [UNSATISFIED_QUORUM_FOR_PREVIOUS_WRITE] = TRUE,
  [REPLICA_IS_NOT_IN_QUORUM] = TRUE,
  [RECEIVED_ERROR_TOO_MANY_REQUESTS] = TRUE,
  [ALL_REPLICAS_ARE_STALE] = TRUE,
  [PART_IS_TEMPORARILY_LOCKED] = TRUE,
  [REPLICA_STATUS_CHANGED] = TRUE,
  [ALL_REPLICAS_LOST] = TRUE,
  [NOT_A_LEADER] = TRUE,
  [DISTRIBUTED_TOO_MANY_PENDING_BYTES] = TRUE,
  [ZERO_COPY_REPLICATION_ERROR] = TRUE,
  [CANNOT_CONNECT_NATS] = TRUE,
  [AWS_ERROR] = TRUE,
  [GCP_ERROR] = TRUE,
  [GOOGLE_CLOUD_ERROR] = TRUE,
  [SERVER_OVERLOADED] = TRUE,
  [KEEPER_EXCEPTION] = TRUE,
  [INCORRECT_QUERY] = TRUE,
  [UNKNOWN_IDENTIFIER] = TRUE,
  [UNKNOWN_TABLE] = TRUE,
  [UNKNOWN_DATABASE] = TRUE
};

// Drop immediately (permanent data/query issues)
static const gboolean CLICKHOUSE_ERRORS_DROP_IMMEDIATE[] =
{
  [UNSUPPORTED_METHOD] = TRUE,
  [UNSUPPORTED_PARAMETER] = TRUE,
  [CANNOT_PARSE_ESCAPE_SEQUENCE] = TRUE,
  [CANNOT_PARSE_QUOTED_STRING] = TRUE,
  [BAD_ARGUMENTS] = TRUE,
  [SYNTAX_ERROR] = TRUE,
  [UNKNOWN_FUNCTION] = TRUE,
  [CANNOT_PARSE_TEXT] = TRUE,
  [CANNOT_PARSE_DATE] = TRUE,
  [CANNOT_PARSE_DATETIME] = TRUE,
  [CANNOT_PARSE_UUID] = TRUE,
  [INCORRECT_NUMBER_OF_COLUMNS] = TRUE,
  [NUMBER_OF_COLUMNS_DOESNT_MATCH] = TRUE,
  [SIZE_OF_FIXED_STRING_DOESNT_MATCH] = TRUE,
  [DATA_TYPE_CANNOT_BE_USED_IN_TABLES] = TRUE,
  [DATA_TYPE_CANNOT_BE_USED_IN_KEY] = TRUE,
  [CANNOT_PARSE_PROTOBUF_SCHEMA] = TRUE,
  [NO_COLUMN_SERIALIZED_TO_REQUIRED_PROTOBUF_FIELD] = TRUE,
  [PROTOBUF_BAD_CAST] = TRUE,
  [MULTIPLE_COLUMNS_SERIALIZED_TO_SAME_PROTOBUF_FIELD] = TRUE,
  [DATA_TYPE_INCOMPATIBLE_WITH_PROTOBUF_FIELD] = TRUE,
  [CORRUPTED_DATA] = TRUE,
  [BAD_DATA_PART_NAME] = TRUE,
  [POTENTIALLY_BROKEN_DATA_PART] = TRUE,
  [NO_DATA_TO_INSERT] = TRUE,
  [INCORRECT_DATA] = TRUE,
  [UNKNOWN_FORMAT] = TRUE,
  [CANNOT_PARSE_NUMBER] = TRUE,
  [TYPE_MISMATCH] = TRUE,
  [LOGICAL_ERROR] = TRUE
};

#define NUM_CLICKHOUSE_ERRORS_RETRY_FOREVER G_N_ELEMENTS(CLICKHOUSE_ERRORS_RETRY_FOREVER)
#define NUM_CLICKHOUSE_ERRORS_DROP_IMMEDIATE G_N_ELEMENTS(CLICKHOUSE_ERRORS_DROP_IMMEDIATE)

ErrorAction classify_clickhouse_error(ClickHouseErrorCode code)
{
  if (code >= 0 &&
      code < NUM_CLICKHOUSE_ERRORS_RETRY_FOREVER &&
      CLICKHOUSE_ERRORS_RETRY_FOREVER[code])
    return ACTION_RETRY_FOREVER;

  if (code >= 0 &&
      code < NUM_CLICKHOUSE_ERRORS_DROP_IMMEDIATE &&
      CLICKHOUSE_ERRORS_DROP_IMMEDIATE[code])
    return ACTION_DROP;

  return ACTION_RETRY_THEN_DROP;
}

LogThreadedResult error_action_to_logthreaded_result(ErrorAction error_action)
{
  switch (error_action)
    {
    case ACTION_RETRY_FOREVER:
      return LTR_NOT_CONNECTED;
      break;
    case ACTION_RETRY_THEN_DROP:
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

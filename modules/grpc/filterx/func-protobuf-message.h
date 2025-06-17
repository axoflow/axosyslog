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

#ifndef FUNC_PROTOBUF_MESSAGE_H_INCLUDED
#define FUNC_PROTOBUF_MESSAGE_H_INCLUDED

#include "plugin.h"
#include "filterx/expr-function.h"

#define FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA_FILE "schema_file"
#define FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA "schema"

#define FILTERX_FUNC_PROTOBUF_MESSAGE_USAGE "Usage: protobuf_message({dict}, [" \
FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA"={dict}(not yet implemented)," \
FILTERX_FUNC_PROTOBUF_MESSAGE_ARG_NAME_SCHEMA_FILE"={string literal}])"

FILTERX_FUNCTION_DECLARE(protobuf_message);

FilterXExpr *filterx_function_protobuf_message_new(FilterXFunctionArgs *args, GError **error);

#endif

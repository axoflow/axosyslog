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

#include "cfg-parser.h"
#include "plugin.h"
#include "plugin-types.h"
#include "protos/apphook.h"
#include "func-protobuf-message.h"
#include "filterx/expr-function.h"

static Plugin grpc_filterx_plugins[] =
{
  FILTERX_FUNCTION_PLUGIN(protobuf_message),
};

gboolean
grpc_filterx_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, grpc_filterx_plugins, G_N_ELEMENTS(grpc_filterx_plugins));
  grpc_register_global_initializers();
  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "grpc-filterx",
  .version = SYSLOG_NG_VERSION,
  .description = "Generic gRPC related FilterX plugins",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = grpc_filterx_plugins,
  .plugins_len = G_N_ELEMENTS(grpc_filterx_plugins),
};

/*
 * Copyright (c) 2026 Axoflow
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

#ifndef HTTP_SOURCE_CONFIG_H
#define HTTP_SOURCE_CONFIG_H

#include "module-config.h"

/*
 * Configuration-level globals for the http-source module, stored in
 * cfg->module_config (see ModuleConfig).
 */
typedef struct _HTTPSourceConfig
{
  ModuleConfig super;
  GHashTable *listeners;        /* "bind_addr:port" -> HTTPServerListener * */
} HTTPSourceConfig;

HTTPSourceConfig *http_source_config_get(GlobalConfig *cfg);

#endif

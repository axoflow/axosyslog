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

#include "http-source-config.h"
#include "cfg.h"

#define MODULE_CONFIG_KEY "http-source"

static void
http_source_config_free(ModuleConfig *s)
{
  HTTPSourceConfig *self = (HTTPSourceConfig *) s;

  if (self->listeners)
    g_hash_table_unref(self->listeners);

  module_config_free_method(s);
}

static HTTPSourceConfig *
http_source_config_new(void)
{
  HTTPSourceConfig *self = g_new0(HTTPSourceConfig, 1);

  self->super.free_fn = http_source_config_free;
  return self;
}

HTTPSourceConfig *
http_source_config_get(GlobalConfig *cfg)
{
  HTTPSourceConfig *self = g_hash_table_lookup(cfg->module_config, MODULE_CONFIG_KEY);
  if (!self)
    {
      self = http_source_config_new();
      g_hash_table_insert(cfg->module_config, g_strdup(MODULE_CONFIG_KEY), self);
    }
  return self;
}

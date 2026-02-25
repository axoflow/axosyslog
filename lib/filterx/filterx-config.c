/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "filterx/filterx-config.h"
#include "filterx/filterx-eval.h"
#include "cfg.h"

#define MODULE_CONFIG_KEY "filterx"

static void
filterx_config_free(ModuleConfig *s)
{
  FilterXConfig *self = (FilterXConfig *) s;

  filterx_env_clear(&self->global_env);
  module_config_free_method(s);
}

FilterXConfig *
filterx_config_new(GlobalConfig *cfg)
{
  FilterXConfig *self = g_new0(FilterXConfig, 1);

  self->super.free_fn = filterx_config_free;
  filterx_env_init(&self->global_env);
  return self;
}

FilterXConfig *
filterx_config_get(GlobalConfig *cfg)
{
  FilterXConfig *fxc = g_hash_table_lookup(cfg->module_config, MODULE_CONFIG_KEY);
  if (!fxc)
    {
      fxc = filterx_config_new(cfg);
      g_hash_table_insert(cfg->module_config, g_strdup(MODULE_CONFIG_KEY), fxc);
    }
  return fxc;
}

void
filterx_config_freeze_object(GlobalConfig *cfg, FilterXObject **object)
{
  if (!cfg)
    {
      filterx_object_freeze(object, NULL);
      return;
    }
  FilterXConfig *fxc = filterx_config_get(cfg);
  filterx_env_freeze_object(&fxc->global_env, object);
}

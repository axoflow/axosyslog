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
#include "filterx-config.h"
#include "syslog-ng.h"
#include "messages.h"
#include "cfg.h"
#include "apphook.h"

#define MODULE_CONFIG_KEY "filterx"

static void
filterx_config_free(ModuleConfig *s)
{
  FilterXConfig *self = (FilterXConfig *) s;

  g_ptr_array_unref(self->weak_refs);
  g_ptr_array_unref(self->frozen_objects);
  g_hash_table_unref(self->frozen_deduplicated_objects);
  module_config_free_method(s);
}

#if SYSLOG_NG_ENABLE_JIT

static void
_finalize_jit(gint type, gpointer user_data)
{
  FilterXJIT *jit = user_data;

  GError *error = NULL;
  if (filterx_jit_finalize(jit, &error))
    return;

  msg_warning("FilterX JIT module finalization failed, falling back to interpreted evaluation",
              evt_tag_str("error", error ? error->message : "unknown"));
  g_clear_error(&error);

#if SYSLOG_NG_ENABLE_DEBUG
  g_assert_not_reached();
#endif
}

#endif

static inline FilterXJIT *
_create_jit(void)
{
#if SYSLOG_NG_ENABLE_JIT
  GError *error = NULL;
  FilterXJIT *jit = filterx_jit_new(FILTERX_JIT_MODULE_NAME, &error);

  if (!jit)
    {
      msg_error("Error creating FilterX JIT compiler", evt_tag_str("error", error->message));
      g_clear_error(&error);
      return NULL;
    }

  register_application_hook(AH_CONFIG_POST_INIT, _finalize_jit, jit, AHM_RUN_ONCE);

  return jit;
#else
  return NULL;
#endif
}

static gboolean
filterx_config_init(ModuleConfig *s, GlobalConfig *cfg)
{
  FilterXConfig *self = (FilterXConfig *) s;

  if (self->enable_jit == TRUE)
    {
#if !SYSLOG_NG_ENABLE_JIT
      msg_error("Error enabling filterx-jit(), AxoSyslog was compiled without FilterX JIT support");
      return FALSE;
#endif
    }

  if (self->enable_jit == -1)
    self->enable_jit = TRUE;

  if (self->enable_jit)
    self->jit = _create_jit();

  return TRUE;
}

static void
filterx_config_deinit(ModuleConfig *s, GlobalConfig *cfg)
{
  FilterXConfig *self = (FilterXConfig *) s;

  filterx_jit_free(self->jit);
}

FilterXConfig *
filterx_config_new(GlobalConfig *cfg)
{
  FilterXConfig *self = g_new0(FilterXConfig, 1);

  self->super.init = filterx_config_init;
  self->super.deinit = filterx_config_deinit;
  self->super.free_fn = filterx_config_free;
  self->frozen_objects = g_ptr_array_new_with_free_func((GDestroyNotify) _filterx_object_unfreeze_and_free);
  self->frozen_deduplicated_objects = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                            (GDestroyNotify)_filterx_object_unfreeze_and_free);
  self->weak_refs = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_object_unref);
  self->enable_jit = -1;
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

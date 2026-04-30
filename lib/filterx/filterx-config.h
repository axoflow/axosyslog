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
#ifndef FILTERX_CONFIG_H_INCLUDED
#define FILTERX_CONFIG_H_INCLUDED 1

#include "module-config.h"
#include "filterx/filterx-object.h"
#include "filterx/jit/jit.h"
#include "filterx/object-string.h"

#include <string.h>

typedef struct _FilterXConfig
{
  ModuleConfig super;
  GPtrArray *frozen_objects;
  GHashTable *frozen_deduplicated_objects;
  GPtrArray *weak_refs;

  gboolean enable_jit;
  FilterXJITDebugInfo jit_debug_info;
  FilterXJIT *jit;
} FilterXConfig;

FilterXConfig *filterx_config_get(GlobalConfig *cfg);

static inline void
filterx_config_enable_jit(FilterXConfig *self, gboolean enable)
{
  self->enable_jit = enable;
}

static inline gboolean
filterx_config_lookup_jit_debug_info(const gchar *name, FilterXJITDebugInfo *mode)
{
  if (strcmp(name, "filterx") == 0)
    *mode = FILTERX_JIT_DEBUG_INFO_FILTERX;
#if FILTERX_JIT_DEBUG_INFO_LLVM_IR_SUPPORTED
  else if (strcmp(name, "llvm-ir") == 0)
    *mode = FILTERX_JIT_DEBUG_INFO_LLVM_IR;
#else
  else if (strcmp(name, "llvm-ir") == 0)
    {
      msg_error("filterx-jit-debug-info(llvm-ir) is not supported on this platform");
      return FALSE;
    }
#endif
  else
    return FALSE;

  return TRUE;
}

static inline void
filterx_config_set_jit_debug_info(FilterXConfig *self, FilterXJITDebugInfo mode)
{
  self->jit_debug_info = mode;
}

#endif

/*
 * Copyright (c) 2025-2026 László Várady
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

#ifndef FILTERX_JIT_PRIVATE_H
#define FILTERX_JIT_PRIVATE_H

#include "syslog-ng.h"

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/ffi.h"

#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/DebugInfo.h>

struct _FilterXJIT
{
  gchar *mod_name;

  LLVMOrcThreadSafeContextRef ts_ctx;
  LLVMContextRef ctx;
  LLVMModuleRef mod;
  LLVMBuilderRef ir;
  LLVMOrcLLJITRef j;

  FilterXIRValue current_ir_block;
  LLVMMetadataRef current_debug_info_block;

  FilterXJITFFI ffi;

  FilterXJITDebugInfo debug_info_mode;
  LLVMDIBuilderRef debug;
  gint debug_ir_text_memfd;

  gboolean mod_finalized;
};

#else

struct _FilterXJIT
{
  guint8 dummy;
};

#endif

#endif

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

#ifndef FILTERX_JIT_H
#define FILTERX_JIT_H

#include "syslog-ng.h"
#include "filterx/filterx-object.h"

/*
 * FilterX JIT:
 * 1. Create a single FilterXJIT instance per configuration (optimizations will be done globally)
 * 2. Generate IR code for each FilterX block (filterx_jit_ir_*() methods)
 * 3. Finalize the FilterXJIT instance (filterx_jit_finalize()):
 *    - no IR codegen is possible from this point,
 *    - JIT-compiled FilterX blocks are ready to be used (filterx_jit_lookup())
 */

typedef struct _FilterXJIT FilterXJIT;

#define FILTERX_JIT_MODULE_NAME "filterx::jit"

#if SYSLOG_NG_ENABLE_JIT
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include <llvm-c/LLJIT.h>
typedef LLVMValueRef FilterXIRValue;
typedef LLVMBasicBlockRef FilterXIRSequence;
typedef LLVMTypeRef FilterXIRType;
typedef LLVMBuilderRef FilterXIRBuilder;
typedef LLVMOrcJITTargetAddress FilterXJITAddress;
typedef LLVMContextRef FilterXJITCtx;
#else
typedef gpointer FilterXIRValue;
typedef gpointer FilterXIRSequence;
typedef gpointer FilterXIRType;
typedef gpointer FilterXIRBuilder;
typedef guint64 FilterXJITAddress;
typedef gpointer FilterXJITCtx;
#endif

typedef struct _FilterXJITExecState
{
  guint8 todo;
} FilterXJITExecState;

typedef FilterXObject *(*FilterXJITExecFunc)(FilterXJITExecState *state);


FilterXJIT *filterx_jit_new(const gchar *module_name, GError **error);
void filterx_jit_free(FilterXJIT *self);

/* IR */
FilterXIRBuilder filterx_jit_get_ir_builder(FilterXJIT *self);
void filterx_jit_ir_add_new_block(FilterXJIT *self, const gchar *block_name);
void filterx_jit_ir_finish_current_block(FilterXJIT *self, FilterXIRValue result);
FilterXIRValue filterx_jit_ir_get_current_block(FilterXJIT *self);

FilterXIRSequence filterx_jit_ir_create_sequence(FilterXJIT *self, const gchar *seq_name, FilterXIRValue block);
void filterx_jit_ir_set_insert_point_to_sequence_tail(FilterXJIT *self, FilterXIRSequence sequence);
void filterx_jit_ir_add_sequence_to_block(FilterXJIT *self, FilterXIRSequence seq, FilterXIRValue block);
FilterXIRSequence filterx_jit_ir_add_new_sequence_to_block(FilterXJIT *self, const gchar *seq_name, FilterXIRValue block);

void filterx_jit_ir_set_source_location(FilterXJIT *self, const gchar *file, gint line, gint column);

/* JIT */
gboolean filterx_jit_finalize(FilterXJIT *self, GError **error);
FilterXJITAddress filterx_jit_lookup(FilterXJIT *self, const gchar *block_name, GError **error);

void filterx_jit_global_init(void);
void filterx_jit_global_deinit(void);

#endif

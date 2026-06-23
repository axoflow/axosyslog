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

#include "filterx/jit/jit.h"
#include "filterx/jit/jit-private.h"
#include "filterx/jit/bc-loader.h"
#include "filterx/jit/ffi.h"
#include "filterx/filterx-scope-var-layout.h"
#include "messages.h"

#if SYSLOG_NG_ENABLE_JIT

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Linker.h>
#include <llvm-c/Orc.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/OrcEE.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Support.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define DEBUG_VERSION_KEY "Debug Info Version"
#define DWARF_VERSION_KEY "Dwarf Version"

static inline void
_fxjit_error(const gchar *error_msg, GError **error)
{
  GQuark fx_jit_error = g_quark_from_static_string("filterx-jit");
  g_set_error(error, fx_jit_error, 0, "FilterX JIT error: %s", error_msg);
}

static inline void
_llvm_error_to_fxjit_error(LLVMErrorRef err, GError **error)
{
  gchar *message = LLVMGetErrorMessage(err);
  _fxjit_error(message, error);
  LLVMDisposeErrorMessage(message);
}

static FilterXJITPendingBlock *
_pending_block_new(LLVMModuleRef mod, const gchar *name)
{
  FilterXJITPendingBlock *self = g_new0(FilterXJITPendingBlock, 1);
  self->bc = LLVMWriteBitcodeToMemoryBuffer(mod);
  self->name = g_strdup(name);
  return self;
}

static void
_pending_block_free(FilterXJITPendingBlock *self)
{
  if (self->bc)
    LLVMDisposeMemoryBuffer(self->bc);
  g_free(self->name);
  g_free(self);
}

static inline void
_assert_verify_block(FilterXJIT *self, FilterXIRValue block)
{
  LLVMVerifyFunction(block, LLVMAbortProcessAction);
}

static inline gboolean
_verify_module(FilterXJIT *self, GError **error)
{
  gchar *error_msg = NULL;
  gboolean module_broken = LLVMVerifyModule(self->mod, LLVMReturnStatusAction, &error_msg);
  if (module_broken)
    {
      _fxjit_error(error_msg, error);
      LLVMDisposeMessage(error_msg);
      return FALSE;
    }

  /* LLVMVerifyModule() allocates an error string even when there were no errors */
  LLVMDisposeMessage(error_msg);
  return TRUE;
}

static LLVMErrorRef
_optimize_module(gpointer s, LLVMModuleRef mod)
{
  LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();
  LLVMPassBuilderOptionsSetInlinerThreshold(options, 512);

  FilterXJIT *self = (FilterXJIT *) s;
  msg_trace("FilterXJIT optimize module", evt_tag_str("module_name", self->mod_name));

  const gchar *pass_override = g_getenv("SYSLOG_NG_FILTERX_JIT_PASSES");
  LLVMErrorRef err = LLVMRunPasses(mod, pass_override ? : "default<O3>", self->tm, options);

  LLVMDisposePassBuilderOptions(options);
  return err;
}

static LLVMErrorRef
_optimize_transform(gpointer s, LLVMOrcThreadSafeModuleRef *thr_mod, LLVMOrcMaterializationResponsibilityRef mr)
{
  return LLVMOrcThreadSafeModuleWithModuleDo(*thr_mod, _optimize_module, s);
}

FilterXIRBuilder
filterx_jit_get_ir_builder(FilterXJIT *self)
{
  g_assert(!self->mod_finalized);
  return self->ir;
}

static inline LLVMMetadataRef
_create_debug_info_block(FilterXJIT *self, const gchar *block_name, const gchar *file, gint line)
{
  LLVMMetadataRef di_file = LLVMDIBuilderCreateFile(self->debug, file, strlen(file), "", 0);
  LLVMMetadataRef subroutine_ty = LLVMDIBuilderCreateSubroutineType(self->debug, di_file, NULL, 0, LLVMDIFlagZero);

  return LLVMDIBuilderCreateFunction(self->debug, di_file, block_name, strlen(block_name),
                                     block_name, strlen(block_name), di_file, line, subroutine_ty, FALSE, TRUE, line,
                                     LLVMDIFlagZero, FALSE);
}

void
filterx_jit_ir_set_source_location(FilterXJIT *self, const gchar *file, gint line, gint column)
{
  g_assert(!self->mod_finalized);

  if (self->debug_info_mode != FILTERX_JIT_DEBUG_INFO_FILTERX)
    return;

  if (!self->current_debug_info_block)
    {
      const gchar *block_name = LLVMGetValueName(self->current_ir_block);
      self->current_debug_info_block = _create_debug_info_block(self, block_name, file, line);
      LLVMSetSubprogram(self->current_ir_block, self->current_debug_info_block);
    }

  LLVMMetadataRef loc = LLVMDIBuilderCreateDebugLocation(self->ctx, line, column, self->current_debug_info_block, NULL);
  LLVMSetCurrentDebugLocation2(self->ir, loc);
}

static inline gchar *
_create_fully_qualified_block_name(FilterXJIT *self, const gchar *block_name)
{
  return g_strconcat(self->mod_name, "::", block_name, NULL);
}

static inline LLVMTypeRef
_block_function_type(FilterXJIT *self)
{
  LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(self->ctx, 0);
  LLVMTypeRef params[] = { ptr_ty };
  return LLVMFunctionType(ptr_ty, params, G_N_ELEMENTS(params), FALSE);
}

static inline void
_set_unwind_attributes(FilterXJIT *self, FilterXIRValue block)
{
  LLVMAddTargetDependentFunctionAttr(block, "frame-pointer", "non-leaf");

  const gchar *uwtable = "uwtable";
  unsigned uwtable_kind = LLVMGetEnumAttributeKindForName(uwtable, strlen(uwtable));
  LLVMAttributeRef uw = LLVMCreateEnumAttribute(self->ctx, uwtable_kind, 2 /* UWTableKind::Async */);
  LLVMAddAttributeAtIndex(block, LLVMAttributeFunctionIndex, uw);
}

static inline void
_copy_attrs_at_index(LLVMValueRef src, LLVMValueRef dest, unsigned idx)
{
  unsigned count = LLVMGetAttributeCountAtIndex(src, idx);
  if (!count)
    return;

  LLVMAttributeRef *attrs = g_alloca(count * sizeof(LLVMAttributeRef));
  LLVMGetAttributesAtIndex(src, idx, attrs);
  for (unsigned i = 0; i < count; i++)
    LLVMAddAttributeAtIndex(dest, idx, attrs[i]);
}

static inline void
_inherit_libfilterx_function_attributes(FilterXJIT *self, FilterXIRValue dest)
{
  if (!self->libfilterx)
    return;

  LLVMValueRef tmpl = LLVMGetNamedFunction(self->libfilterx, "fx_jit_attribute_template");
  if (!tmpl || LLVMIsDeclaration(tmpl))
    return;

  _copy_attrs_at_index(tmpl, dest, LLVMAttributeFunctionIndex);

  if (LLVMGetTypeKind(LLVMGetReturnType(LLVMGlobalGetValueType(dest))) != LLVMVoidTypeKind)
    _copy_attrs_at_index(tmpl, dest, LLVMAttributeReturnIndex);

  unsigned param_count = LLVMCountParams(tmpl);
  for (unsigned paramidx = 1; paramidx <= param_count; paramidx++)
    _copy_attrs_at_index(tmpl, dest, paramidx);
}

static const guint8 _fx_jit_var_uninitialized;

static inline LLVMTypeRef
_variable_storage_type(FilterXJIT *self)
{
  return LLVMArrayType(self->ffi.ptr_ty, self->current_block_variables_size);
}

static FilterXIRValue
_variable_uninitialized_sentinel(FilterXJIT *self)
{
  return fx_jit_emit_const_ptr(self, &_fx_jit_var_uninitialized);
}

FilterXIRValue
filterx_jit_ir_is_variable_uninitialized(FilterXJIT *self, FilterXIRValue variable)
{
  return LLVMBuildICmp(self->ir, LLVMIntEQ, variable, _variable_uninitialized_sentinel(self), "var_is_unloaded");
}

static void
_reset_variables(FilterXJIT *self)
{
  if (!self->current_block_variables)
    return;

  FilterXIRValue sentinel = _variable_uninitialized_sentinel(self);

  LLVMValueRef *elements = g_newa(LLVMValueRef, self->current_block_variables_size);
  for (guint32 i = 0; i < self->current_block_variables_size; i++)
    elements[i] = sentinel;

  LLVMBuildStore(self->ir, LLVMConstArray(self->ffi.ptr_ty, elements, self->current_block_variables_size),
                 self->current_block_variables);
}

static inline void
_init_variables(FilterXJIT *self, FilterXScopeVariableLayout *layout)
{
  guint32 num_variables = layout ? layout->num_variables : 0;
  self->current_block_variables_size = num_variables;
  self->current_block_variables = NULL;
  if (num_variables > 0)
    {
      self->current_block_variables = LLVMBuildAlloca(self->ir, _variable_storage_type(self), "variables");
      _reset_variables(self);
    }
}

void
filterx_jit_ir_add_new_block(FilterXJIT *self, const gchar *block_name, FilterXScopeVariableLayout *layout)
{
  g_assert(!self->mod_finalized);
  g_assert(!self->current_ir_block);

  gchar *fqn = _create_fully_qualified_block_name(self, block_name);
  self->current_ir_block = LLVMAddFunction(self->mod, fqn, _block_function_type(self));
  _set_unwind_attributes(self, self->current_ir_block);
  _inherit_libfilterx_function_attributes(self, self->current_ir_block);
  g_free(fqn);

  self->current_eval_context = LLVMGetParam(self->current_ir_block, 0);
  LLVMSetValueName2(self->current_eval_context, "eval_context", strlen("eval_context"));

  FilterXIRSequence entry = filterx_jit_ir_add_new_sequence_to_block(self, "entry", self->current_ir_block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(self, entry);

  _init_variables(self, layout);
}

FilterXIRValue
filterx_jit_ir_get_variable(FilterXJIT *self, gint scope_var_idx)
{
  g_assert(!self->mod_finalized);
  g_assert(scope_var_idx >= 0 && scope_var_idx < self->current_block_variables_size);

  LLVMValueRef indices[] =
  {
    LLVMConstInt(self->ffi.i32_ty, 0, FALSE),
    LLVMConstInt(self->ffi.i32_ty, scope_var_idx, FALSE),
  };

  gchar name[32];
  g_snprintf(name, sizeof(name), "var_%d", scope_var_idx);
  return LLVMBuildGEP2(self->ir, _variable_storage_type(self), self->current_block_variables,
                       indices, G_N_ELEMENTS(indices), name);
}

void
filterx_jit_ir_clear_variables(FilterXJIT *self)
{
  g_assert(!self->mod_finalized);

  _reset_variables(self);
}

FilterXIRValue
filterx_jit_ir_get_eval_context(FilterXJIT *self)
{
  g_assert(!self->mod_finalized);
  g_assert(self->current_eval_context);
  return self->current_eval_context;
}

void
filterx_jit_ir_finish_current_block(FilterXJIT *self, FilterXIRValue result)
{
  g_assert(!self->mod_finalized);
  g_assert(self->current_ir_block);

  LLVMBuildRet(self->ir, result);

  if (self->current_debug_info_block)
    LLVMDIBuilderFinalizeSubprogram(self->debug, self->current_debug_info_block);

  _assert_verify_block(self, self->current_ir_block);

  self->current_block_variables = NULL;
  self->current_block_variables_size = 0;

  self->current_ir_block = NULL;
  self->current_eval_context = NULL;
  self->current_debug_info_block = NULL;
  LLVMSetCurrentDebugLocation2(self->ir, NULL);
}

FilterXIRValue
filterx_jit_ir_get_current_block(FilterXJIT *self)
{
  g_assert(!self->mod_finalized);
  g_assert(self->current_ir_block);

  return self->current_ir_block;
}

FilterXIRSequence
filterx_jit_ir_create_sequence(FilterXJIT *self, const gchar *seq_name, FilterXIRValue block)
{
  g_assert(!self->mod_finalized);
  return LLVMCreateBasicBlockInContext(self->ctx, seq_name);
}

void
filterx_jit_ir_set_insert_point_to_sequence_tail(FilterXJIT *self, FilterXIRSequence sequence)
{
  g_assert(!self->mod_finalized);
  LLVMPositionBuilderAtEnd(self->ir, sequence);
}

void
filterx_jit_ir_add_sequence_to_block(FilterXJIT *self, FilterXIRSequence seq, FilterXIRValue block)
{
  g_assert(!self->mod_finalized);
  LLVMAppendExistingBasicBlock(block, seq);
}

FilterXIRSequence
filterx_jit_ir_add_new_sequence_to_block(FilterXJIT *self, const gchar *seq_name, FilterXIRValue block)
{
  g_assert(!self->mod_finalized);
  return LLVMAppendBasicBlockInContext(self->ctx, block, seq_name);
}

#if FILTERX_JIT_DEBUG_INFO_LLVM_IR_SUPPORTED

static gint
_count_newlines(const gchar *text)
{
  gint n = 0;
  for (const gchar *p = text; *p; p++)
    if (*p == '\n')
      n++;
  return n;
}

static void
_string_append_line_count(GString *str, guint *line, const gchar *text)
{
  g_string_append(str, text);
  *line += _count_newlines(text);
}

static void G_GNUC_PRINTF(3, 4)
_string_append_printf_line_count(GString *str, guint *line, const char *format, ...)
{
  gsize orig_len = str->len;

  va_list args;
  va_start(args, format);
  g_string_append_vprintf(str, format, args);
  va_end(args);

  *line += _count_newlines(str->str + orig_len);
}

/*
 * LLVM IR debug info
 * Renders each function with one instruction per line into a memfd.
 */
static gboolean
_emit_llvm_ir_debug_info(FilterXJIT *self, GError **error)
{
  self->debug_ir_text_memfd = memfd_create(self->mod_name, MFD_CLOEXEC);
  if (self->debug_ir_text_memfd < 0)
    {
      _fxjit_error("memfd_create failed", error);
      return FALSE;
    }

  gchar path[64];
  g_snprintf(path, sizeof(path), "/proc/%d/fd/%d", (int) getpid(), self->debug_ir_text_memfd);
  LLVMMetadataRef di_file = LLVMDIBuilderCreateFile(self->debug, path, strlen(path), "", 0);

  GString *ir_text = g_string_sized_new(1024);
  guint line = 0;

  for (LLVMValueRef fn = LLVMGetFirstFunction(self->mod); fn; fn = LLVMGetNextFunction(fn))
    {
      if (!LLVMGetFirstBasicBlock(fn))
        continue;

      const gchar *fn_name = LLVMGetValueName(fn);
      _string_append_printf_line_count(ir_text, &line, "define @\"%s\" {\n", fn_name);

      LLVMMetadataRef subroutine_ty = LLVMDIBuilderCreateSubroutineType(self->debug, di_file, NULL, 0, LLVMDIFlagZero);
      LLVMMetadataRef sp = LLVMDIBuilderCreateFunction(self->debug, di_file, fn_name, strlen(fn_name),
                                                       fn_name, strlen(fn_name), di_file, line, subroutine_ty,
                                                       FALSE, TRUE, line, LLVMDIFlagZero, FALSE);
      LLVMSetSubprogram(fn, sp);

      gboolean first_sequence = TRUE;
      for (FilterXIRSequence seq = LLVMGetFirstBasicBlock(fn); seq; seq = LLVMGetNextBasicBlock(seq))
        {
          if (!first_sequence)
            _string_append_line_count(ir_text, &line, "\n");
          first_sequence = FALSE;

          const gchar *bb_name = LLVMGetBasicBlockName(seq);
          bb_name = bb_name ? bb_name : "";
          _string_append_printf_line_count(ir_text, &line, "%s:\n", bb_name);

          for (LLVMValueRef inst = LLVMGetFirstInstruction(seq); inst; inst = LLVMGetNextInstruction(inst))
            {
              gchar *inst_text = LLVMPrintValueToString(inst);
              _string_append_printf_line_count(ir_text, &line, "%s\n", inst_text);
              LLVMDisposeMessage(inst_text);

              LLVMMetadataRef loc = LLVMDIBuilderCreateDebugLocation(self->ctx, line, 1, sp, NULL);
              LLVMInstructionSetDebugLoc(inst, loc);
            }
        }

      _string_append_line_count(ir_text, &line, "}\n\n");
      LLVMDIBuilderFinalizeSubprogram(self->debug, sp);
    }

  ssize_t written = write(self->debug_ir_text_memfd, ir_text->str, ir_text->len);
  g_string_free(ir_text, TRUE);
  if (written < 0)
    {
      _fxjit_error("Failed to write IR text to memfd", error);
      return FALSE;
    }

  return TRUE;
}

#endif

static gboolean
_link_libfilterx(FilterXJIT *self, GError **error)
{
  if (!self->libfilterx)
    return TRUE;

  LLVMBool link_err = LLVMLinkModules2(self->mod, self->libfilterx);
  /* libfilterx is consumed */
  self->libfilterx = NULL;

  if (link_err)
    {
      _fxjit_error("Failed to link embedded libfilterx bitcode into the JIT module", error);
      return FALSE;
    }

  return TRUE;
}

gboolean
filterx_jit_finalize(FilterXJIT *self, GError **error)
{
  if (self->mod_finalized)
    return TRUE;

#if FILTERX_JIT_DEBUG_INFO_LLVM_IR_SUPPORTED
  if (self->debug_info_mode == FILTERX_JIT_DEBUG_INFO_LLVM_IR)
    {
      if (!_emit_llvm_ir_debug_info(self, error))
        return FALSE;
    }
#endif

  if (self->debug)
    LLVMDIBuilderFinalize(self->debug);

  if (!_link_libfilterx(self, error))
    return FALSE;

  if (!_verify_module(self, error))
    return FALSE;

  LLVMOrcThreadSafeModuleRef ts_mod = LLVMOrcCreateNewThreadSafeModule(self->mod, self->ts_ctx);

  LLVMOrcJITDylibRef jit_dylib = LLVMOrcLLJITGetMainJITDylib(self->j);
  LLVMErrorRef err = LLVMOrcLLJITAddLLVMIRModule(self->j, jit_dylib, ts_mod);
  if (err)
    {
      _llvm_error_to_fxjit_error(err, error);
      self->mod = LLVMCloneModule(self->mod);
      LLVMOrcDisposeThreadSafeModule(ts_mod);
      return FALSE;
    }

  msg_trace("FilterXJIT finalized", evt_tag_str("module_name", self->mod_name));

  self->mod_finalized = TRUE;
  return TRUE;
}

FilterXJITAddress
filterx_jit_lookup(FilterXJIT *self, const gchar *block_name, GError **error)
{
  if (!self->mod_finalized)
    return 0;

  gchar *fqn = _create_fully_qualified_block_name(self, block_name);
  msg_trace("FilterXJIT lookup", evt_tag_str("block", fqn), evt_tag_str("module_name", self->mod_name));

  FilterXJITAddress fx_block_addr = 0;
  LLVMErrorRef err = LLVMOrcLLJITLookup(self->j, &fx_block_addr, fqn);
  g_free(fqn);

  if (err)
    {
      _llvm_error_to_fxjit_error(err, error);
      return 0;
    }

  return fx_block_addr;
}

/*
 * Allow calling into C functions from JIT
 *
 * Currently supports symbols coming from dynamic libraries.
 * TODO: LLVMOrcCreateCustomCAPIDefinitionGenerator() for statically linked symbols
 */
static inline gboolean
_setup_c_symbol_generator(FilterXJIT *self, GError **error)
{
  LLVMOrcDefinitionGeneratorRef gen = NULL;

  LLVMErrorRef err =
    LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(&gen, LLVMOrcLLJITGetGlobalPrefix(self->j), NULL, NULL);

  if (err)
    {
      _llvm_error_to_fxjit_error(err, error);
      return FALSE;
    }

  LLVMOrcJITDylibAddGenerator(LLVMOrcLLJITGetMainJITDylib(self->j), gen);
  return TRUE;
}

static inline void
_setup_optimizations(FilterXJIT *self)
{
  /* Disable optimizations when debugging IR code */
  if (self->debug_info_mode == FILTERX_JIT_DEBUG_INFO_LLVM_IR)
    return;

  LLVMOrcIRTransformLayerRef transform = LLVMOrcLLJITGetIRTransformLayer(self->j);
  LLVMOrcIRTransformLayerSetTransform(transform, _optimize_transform, self);
}

static LLVMOrcObjectLayerRef
_create_object_layer_with_gdb_listener(void *ctx, LLVMOrcExecutionSessionRef es, const char *triple)
{
  LLVMOrcObjectLayerRef object_layer = LLVMOrcCreateRTDyldObjectLinkingLayerWithSectionMemoryManager(es);
  LLVMOrcRTDyldObjectLinkingLayerRegisterJITEventListener(object_layer, LLVMCreateGDBRegistrationListener());
  return object_layer;
}

static LLVMTargetMachineRef
_create_target_machine(FilterXJIT *self, GError **error)
{
  const char *triple = LLVMGetTarget(self->libfilterx);
  char *cpu = LLVMGetHostCPUName();
  char *features = LLVMGetHostCPUFeatures();

  LLVMTargetRef target = NULL;
  char *err_msg = NULL;
  if (LLVMGetTargetFromTriple(triple, &target, &err_msg))
    {
      _fxjit_error(err_msg, error);
      LLVMDisposeMessage(err_msg);
      LLVMDisposeMessage(cpu);
      LLVMDisposeMessage(features);
      return NULL;
    }

  LLVMTargetMachineRef tm = LLVMCreateTargetMachine(target, triple, cpu, features, LLVMCodeGenLevelAggressive,
                                                    LLVMRelocDefault, LLVMCodeModelJITDefault);

  LLVMDisposeMessage(cpu);
  LLVMDisposeMessage(features);

  return tm;
}

static gboolean
_setup_target_machine(FilterXJIT *self, LLVMOrcLLJITBuilderRef jit_builder, GError **error)
{
  LLVMTargetMachineRef tm = _create_target_machine(self, error);
  if (!tm)
    return FALSE;

  LLVMOrcJITTargetMachineBuilderRef tmb = LLVMOrcJITTargetMachineBuilderCreateFromTargetMachine(tm);
  LLVMOrcLLJITBuilderSetJITTargetMachineBuilder(jit_builder, tmb);
  /* tm is consumed */
  tm = NULL;

  self->tm = _create_target_machine(self, error);
  return TRUE;
}

static inline void
_setup_debug_info(FilterXJIT *self, LLVMOrcLLJITBuilderRef jit_builder)
{
  LLVMOrcLLJITBuilderSetObjectLinkingLayerCreator(jit_builder, _create_object_layer_with_gdb_listener, NULL);

  LLVMValueRef di_version = LLVMConstInt(LLVMInt32TypeInContext(self->ctx), LLVMDebugMetadataVersion(), FALSE);
  LLVMValueRef dwarf_version = LLVMConstInt(LLVMInt32TypeInContext(self->ctx), 5, FALSE);

  LLVMAddModuleFlag(self->mod, LLVMModuleFlagBehaviorWarning, DEBUG_VERSION_KEY, strlen(DEBUG_VERSION_KEY),
                    LLVMValueAsMetadata(di_version));
  LLVMAddModuleFlag(self->mod, LLVMModuleFlagBehaviorWarning, DWARF_VERSION_KEY, strlen(DWARF_VERSION_KEY),
                    LLVMValueAsMetadata(dwarf_version));

  self->debug = LLVMCreateDIBuilder(self->mod);

  const gchar *dummy_file_name = "<filterx>";
  LLVMMetadataRef file = LLVMDIBuilderCreateFile(self->debug, dummy_file_name, strlen(dummy_file_name), "", 0);

  const gchar *producer = "AxoSyslog FilterX JIT";
  LLVMDIBuilderCreateCompileUnit(self->debug, LLVMDWARFSourceLanguageC, file, producer, strlen(producer), FALSE, "", 0,
                                 0, "", 0, LLVMDWARFEmissionFull, 0, FALSE, FALSE, "", 0, "", 0);
}

FilterXJIT *
filterx_jit_new(const gchar *module_name, FilterXJITDebugInfo debug_info, GError **error)
{
  FilterXJIT *self = g_new0(FilterXJIT, 1);

  // TODO: use Itanium name mangling and namespaces
  self->mod_name = g_strdup(module_name);
  self->debug_info_mode = debug_info;
  self->debug_ir_text_memfd = -1;
  self->compile.pending_blocks = g_ptr_array_new_with_free_func((GDestroyNotify) _pending_block_free);

#if SYSLOG_NG_HAVE_DECL_LLVMORCCREATENEWTHREADSAFECONTEXTFROMLLVMCONTEXT
  self->ctx = LLVMContextCreate();
  self->ts_ctx = LLVMOrcCreateNewThreadSafeContextFromLLVMContext(self->ctx);
#else
  self->ts_ctx = LLVMOrcCreateNewThreadSafeContext();
  self->ctx = LLVMOrcThreadSafeContextGetContext(self->ts_ctx);
#endif

  self->mod = LLVMModuleCreateWithNameInContext(self->mod_name, self->ctx);
  self->ir = LLVMCreateBuilderInContext(self->ctx);

  self->libfilterx = filterx_jit_load_libfilterx_bitcode(self->ctx, error);
  if (!self->libfilterx)
    goto error;

  LLVMSetTarget(self->mod, LLVMGetTarget(self->libfilterx));
  LLVMSetDataLayout(self->mod, LLVMGetDataLayoutStr(self->libfilterx));

  LLVMOrcLLJITBuilderRef jit_builder = LLVMOrcCreateLLJITBuilder();
  _setup_debug_info(self, jit_builder);
  if (!_setup_target_machine(self, jit_builder, error))
    goto error;

  LLVMErrorRef err = LLVMOrcCreateLLJIT(&self->j, jit_builder);
  if (err)
    {
      _llvm_error_to_fxjit_error(err, error);
      goto error;
    }

  if (!_setup_c_symbol_generator(self, error))
    goto error;

  _setup_optimizations(self);

  filterx_jit_ffi_init(self);

  msg_trace("FilterXJIT created", evt_tag_str("module_name", self->mod_name));

  return self;

error:
  filterx_jit_free(self);
  return NULL;
}

static void
_filterx_jit_compile_free(FilterXJIT *self)
{
  if (self->compile.libfilterx_bc)
    LLVMDisposeMemoryBuffer(self->compile.libfilterx_bc);
  if (self->compile.pending_blocks)
    g_ptr_array_free(self->compile.pending_blocks, TRUE);
}

void
filterx_jit_free(FilterXJIT *self)
{
  if (!self)
    return;

  if (self->debug)
    LLVMDisposeDIBuilder(self->debug);
  if (self->j)
    LLVMOrcDisposeLLJIT(self->j);
  if (self->tm)
    LLVMDisposeTargetMachine(self->tm);
  LLVMDisposeBuilder(self->ir);
  self->ctx = NULL;
  if (!self->mod_finalized)
    LLVMDisposeModule(self->mod);
  if (self->libfilterx)
    LLVMDisposeModule(self->libfilterx);
  LLVMOrcDisposeThreadSafeContext(self->ts_ctx);

  if (self->debug_ir_text_memfd >= 0)
    close(self->debug_ir_text_memfd);

  _filterx_jit_compile_free(self);

  msg_trace("FilterXJIT destroyed", evt_tag_str("module_name", self->mod_name));

  g_free(self->mod_name);
  g_free(self);
}

void
filterx_jit_global_init(void)
{
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  /* For debugging: -time-passes -pass-remarks=inline -pass-remarks-missed=inline -pass-remarks-analysis=inline */
  const gchar *extra_args = g_getenv("SYSLOG_NG_FILTERX_JIT_LLVM_ARGS");
  gchar *cmdline = g_strconcat("syslog-ng ", extra_args, NULL);


  GError *error = NULL;
  gchar **argv = NULL;
  gint argc = 0;
  if (!g_shell_parse_argv(cmdline, &argc, &argv, &error))
    {
      msg_error("Error parsing FilterX JIT LLVM arguments", evt_tag_str("error", error->message));
      g_clear_error(&error);
      goto exit;
    }

  LLVMParseCommandLineOptions(argc, (const gchar **) argv, NULL);

exit:
  g_strfreev(argv);
  g_free(cmdline);
}

void
filterx_jit_global_deinit(void)
{
  LLVMShutdown();
}

#else

FilterXJIT *filterx_jit_new(const gchar *module_name, FilterXJITDebugInfo debug_info, GError **error)
{
  return NULL;
}

void filterx_jit_free(FilterXJIT *self) {}
void filterx_jit_global_init(void) {}
void filterx_jit_global_deinit(void) {}

FilterXIRBuilder filterx_jit_get_ir_builder(FilterXJIT *self)
{
  g_assert_not_reached();
}

void filterx_jit_ir_add_new_block(FilterXJIT *self, const gchar *block_name, FilterXScopeVariableLayout *layout)
{
  g_assert_not_reached();
}

void filterx_jit_ir_finish_current_block(FilterXJIT *self, FilterXIRValue result)
{
  g_assert_not_reached();
}

FilterXIRValue filterx_jit_ir_get_current_block(FilterXJIT *self)
{
  g_assert_not_reached();
}

FilterXIRValue filterx_jit_ir_get_eval_context(FilterXJIT *self)
{
  g_assert_not_reached();
}

FilterXIRValue filterx_jit_ir_get_variable(FilterXJIT *self, gint scope_var_idx)
{
  g_assert_not_reached();
}

FilterXIRValue filterx_jit_ir_is_variable_uninitialized(FilterXJIT *self, FilterXIRValue variable)
{
  g_assert_not_reached();
}

void filterx_jit_ir_clear_variables(FilterXJIT *self)
{
  g_assert_not_reached();
}

gboolean filterx_jit_finalize(FilterXJIT *self, GError **error)
{
  g_assert_not_reached();
}

FilterXJITAddress filterx_jit_lookup(FilterXJIT *self, const gchar *block_name, GError **error)
{
  g_assert_not_reached();
}

#endif

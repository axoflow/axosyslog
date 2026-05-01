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
#include "filterx/jit/ffi.h"

#if SYSLOG_NG_ENABLE_JIT

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Orc.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/OrcEE.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/DebugInfo.h>

#include <stdio.h>
#include <string.h>

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

  // TODO smaller faster set: function(instcombine), etc.
  LLVMErrorRef err = LLVMRunPasses(mod, "default<O2>", NULL, options);

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

void
filterx_jit_ir_add_new_block(FilterXJIT *self, const gchar *block_name)
{
  g_assert(!self->mod_finalized);
  g_assert(!self->current_ir_block);

  gchar *fqn = _create_fully_qualified_block_name(self, block_name);
  self->current_ir_block = LLVMAddFunction(self->mod, fqn, _block_function_type(self));
  _set_unwind_attributes(self, self->current_ir_block);
  g_free(fqn);

  FilterXIRSequence entry = filterx_jit_ir_add_new_sequence_to_block(self, "entry", self->current_ir_block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(self, entry);
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

  self->current_ir_block = NULL;
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

gboolean
filterx_jit_finalize(FilterXJIT *self, GError **error)
{
  if (self->mod_finalized)
    return TRUE;

  if (self->debug)
    LLVMDIBuilderFinalize(self->debug);

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

  self->mod_finalized = TRUE;
  return TRUE;
}

FilterXJITAddress
filterx_jit_lookup(FilterXJIT *self, const gchar *block_name, GError **error)
{
  if (!self->mod_finalized)
    return 0;

  gchar *fqn = _create_fully_qualified_block_name(self, block_name);
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

static inline void
_setup_debug_info(FilterXJIT *self, LLVMOrcLLJITBuilderRef jit_builder)
{
  LLVMOrcLLJITBuilderSetObjectLinkingLayerCreator(jit_builder, _create_object_layer_with_gdb_listener, NULL);

  LLVMValueRef di_version = LLVMConstInt(LLVMInt32TypeInContext(self->ctx), LLVMDebugMetadataVersion(), FALSE);
  LLVMValueRef dwarf_version = LLVMConstInt(LLVMInt32TypeInContext(self->ctx), 4, FALSE);

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
filterx_jit_new(const gchar *module_name, GError **error)
{
  FilterXJIT *self = g_new0(FilterXJIT, 1);

  // TODO: use Itanium name mangling and namespaces
  self->mod_name = g_strdup(module_name);

#if SYSLOG_NG_HAVE_DECL_LLVMORCCREATENEWTHREADSAFECONTEXTFROMLLVMCONTEXT
  self->ctx = LLVMContextCreate();
  self->ts_ctx = LLVMOrcCreateNewThreadSafeContextFromLLVMContext(self->ctx);
#else
  self->ts_ctx = LLVMOrcCreateNewThreadSafeContext();
  self->ctx = LLVMOrcThreadSafeContextGetContext(self->ts_ctx);
#endif

  self->mod = LLVMModuleCreateWithNameInContext(self->mod_name, self->ctx);
  self->ir = LLVMCreateBuilderInContext(self->ctx);

  LLVMOrcLLJITBuilderRef jit_builder = LLVMOrcCreateLLJITBuilder();
  _setup_debug_info(self, jit_builder);

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

  return self;

error:
  filterx_jit_free(self);
  return NULL;
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
  LLVMDisposeBuilder(self->ir);
  self->ctx = NULL;
  if (!self->mod_finalized)
    LLVMDisposeModule(self->mod);
  LLVMOrcDisposeThreadSafeContext(self->ts_ctx);

  g_free(self->mod_name);
  g_free(self);
}

void
filterx_jit_global_init(void)
{
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();
}

void
filterx_jit_global_deinit(void)
{
  LLVMShutdown();
}

#else

FilterXJIT *filterx_jit_new(const gchar *module_name, GError **error) { return NULL; }
void filterx_jit_free(FilterXJIT *self) {}
void filterx_jit_global_init(void) {}
void filterx_jit_global_deinit(void) {}

FilterXIRBuilder filterx_jit_get_ir_builder(FilterXJIT *self) { g_assert_not_reached(); }
void filterx_jit_ir_add_new_block(FilterXJIT *self, const gchar *block_name) { g_assert_not_reached(); }
void filterx_jit_ir_finish_current_block(FilterXJIT *self, FilterXIRValue result) { g_assert_not_reached(); }
FilterXIRValue filterx_jit_ir_get_current_block(FilterXJIT *self) { g_assert_not_reached(); }

gboolean filterx_jit_finalize(FilterXJIT *self, GError **error) { g_assert_not_reached(); }
FilterXJITAddress filterx_jit_lookup(FilterXJIT *self, const gchar *block_name, GError **error) { g_assert_not_reached(); }

#endif

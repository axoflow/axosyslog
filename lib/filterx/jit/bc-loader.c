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

#include "filterx/jit/bc-loader.h"
#include "syslog-ng.h"

#if SYSLOG_NG_ENABLE_JIT

#include <llvm-c/BitReader.h>
#include <llvm-c/Comdat.h>

/* Embedded libfilterx.bc boundary symbols */
extern const gchar _binary_lib_filterx_jit_libfilterx_bc_start[] __attribute__((weak));
extern const gchar _binary_lib_filterx_jit_libfilterx_bc_end[] __attribute__((weak));

/*
 * From the linker’s perspective, an "available_externally" global is equivalent to an external declaration.
 * They exist to allow inlining and other optimizations to take place given knowledge of the definition of the global,
 * which is known to be somewhere outside the module.
 */
static void
_mark_symbol_available_externally(LLVMValueRef symbol)
{
  /* The "available_externally" linkage type is only allowed on definitions, not declarations. */
  if (LLVMIsDeclaration(symbol))
    return;

  if (LLVMGetLinkage(symbol) != LLVMExternalLinkage)
    return;

  LLVMSetLinkage(symbol, LLVMAvailableExternallyLinkage);
  LLVMSetComdat(symbol, NULL);
}

static void
_mark_function_inline(LLVMContextRef ctx, LLVMValueRef fn)
{
  if (LLVMIsDeclaration(fn))
    return;

  const gchar *inline_str = "inlinehint";
  guint inline_kind = LLVMGetEnumAttributeKindForName(inline_str, strlen(inline_str));
  LLVMAttributeRef inline_attr = LLVMCreateEnumAttribute(ctx, inline_kind, 0);
  LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, inline_attr);
}

static void
_modify_symbols(LLVMContextRef ctx, LLVMModuleRef mod)
{
  for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn; fn = LLVMGetNextFunction(fn))
    {
      const gchar *name = LLVMGetValueName(fn);
      if (g_strcmp0(name, "fx_jit_attribute_template") == 0)
        continue;

      _mark_function_inline(ctx, fn);
      _mark_symbol_available_externally(fn);
    }

  for (LLVMValueRef g = LLVMGetFirstGlobal(mod); g; g = LLVMGetNextGlobal(g))
    _mark_symbol_available_externally(g);
}

LLVMModuleRef
filterx_jit_load_libfilterx_bitcode(LLVMContextRef ctx, GError **error)
{
  GQuark fx_jit_error = g_quark_from_static_string("filterx-jit");
  if (!_binary_lib_filterx_jit_libfilterx_bc_start || !_binary_lib_filterx_jit_libfilterx_bc_end)
    {
      g_set_error(error, fx_jit_error, 0,
                  "FilterX JIT error: embedded libfilterx bitcode is not linked into this binary");
      return NULL;
    }

  gsize bc_size = (gsize) (_binary_lib_filterx_jit_libfilterx_bc_end - _binary_lib_filterx_jit_libfilterx_bc_start);

  LLVMMemoryBufferRef buf = LLVMCreateMemoryBufferWithMemoryRange(_binary_lib_filterx_jit_libfilterx_bc_start, bc_size,
                                                                  "libfilterx.bc", FALSE);

  LLVMModuleRef mod = NULL;
  LLVMBool err = LLVMParseBitcodeInContext2(ctx, buf, &mod);
  LLVMDisposeMemoryBuffer(buf);

  if (err)
    {
      g_set_error(error, fx_jit_error, 0, "FilterX JIT error: failed to parse embedded libfilterx bitcode");
      return NULL;
    }

  _modify_symbols(ctx, mod);
  return mod;
}

#endif

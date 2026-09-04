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
#include "filterx/expr-set-subscript-private.h"
#include "filterx/expr-set-subscript-devirt.h"
#include "filterx/filterx-eval.h"

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"

typedef gboolean (*FilterXJITTypedSetSubscript)(FilterXObject *object, FilterXObject *key, FilterXObject **value);

/* _set_subscript_compile() guards @cloned and @key against NULL, only @object can still
 * fail here.
 *
 * @cloned is the already forked right hand side, see the NOTE in _set_subscript_compile().
 * We own it, together with @object and @key, and we return it on success. */
static inline __attribute__((always_inline)) FilterXObject *
_do_set_subscript(FilterXObject *object, FilterXObject *key, FilterXObject *cloned,
                  FilterXJITTypedSetSubscript helper)
{
  if (!object)
    {
      filterx_eval_push_error_static_info("Failed to set element of object",
                                          "Failed to evaluate expression");
      goto error;
    }
  if (!helper(object, key, &cloned))
    {
      filterx_eval_push_error("Object set-subscript failed", key);
      filterx_eval_push_error_static_info("Failed to set element of object",
                                          "set-subscript() method failed");
      goto error;
    }

  filterx_object_unref(object);
  filterx_object_unref(key);
  return cloned;

error:
  filterx_object_unref(object);
  filterx_object_unref(key);
  filterx_object_unref(cloned);
  return NULL;
}

__attribute__((used))
FilterXObject *
fx_jit_typed_set_subscript_dict(FilterXObject *object, FilterXObject *key, FilterXObject *cloned)
{
  return _do_set_subscript(object, key, cloned, filterx_dict_set_subscript);
}

__attribute__((used))
FilterXObject *
fx_jit_typed_set_subscript_list(FilterXObject *object, FilterXObject *key, FilterXObject *cloned)
{
  return _do_set_subscript(object, key, cloned, filterx_list_set_subscript_by_key);
}

FilterXIRValue
_set_subscript_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);

  const gchar *fn_name;
  switch (self->object->static_type)
    {
    case FILTERX_STATIC_TYPE_DICT:
      fn_name = "fx_jit_typed_set_subscript_dict";
      break;
    case FILTERX_STATIC_TYPE_LIST:
      fn_name = "fx_jit_typed_set_subscript_list";
      break;
    default:
      return fx_jit_emit_expr_eval(jit, s);
    }

  /* the keyless append form takes the interpreter path */
  if (!self->key)
    return fx_jit_emit_expr_eval(jit, s);

  FilterXIRValue result_slot = filterx_jit_ir_add_stack_slot(jit, ffi->ptr_ty, "result");
  LLVMBuildStore(ir, LLVMConstNull(ffi->ptr_ty), result_slot);

  FilterXIRSequence finish = filterx_jit_ir_create_sequence(jit, "set_subscript_finish", block);

  /* NOTE: the fork has to happen before we evaluate the lhs, so that the lhs will notice it
   * is shared and can clone accordingly.  This is needed to make sure something like
   * `d["sub"] = d` works.  _set_subscript() in the interpreter forks new_value for the very
   * same reason.
   */
  FilterXIRValue new_value = filterx_expr_compile_or_eval(self->new_value, jit);

  FilterXIRSequence rhs_null = filterx_jit_ir_create_sequence(jit, "set_subscript_rhs_null", block);
  FilterXIRSequence eval_key = filterx_jit_ir_create_sequence(jit, "set_subscript_eval_key", block);

  /* mirrors _set_subscript_eval(): if (!new_value) goto finish; the key and the object
   * are not evaluated */
  LLVMBuildCondBr(ir, LLVMBuildIsNull(ir, new_value, "rhs_is_null"), rhs_null, eval_key);

  filterx_jit_ir_add_sequence_to_block(jit, rhs_null, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, rhs_null);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, eval_key, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, eval_key);
  FilterXIRValue key = filterx_expr_compile_or_eval(self->key, jit);

  FilterXIRSequence key_null = filterx_jit_ir_create_sequence(jit, "set_subscript_key_null", block);
  FilterXIRSequence do_set = filterx_jit_ir_create_sequence(jit, "set_subscript_do_set", block);

  /* if (!key) { unref(new_value); goto finish; } */
  LLVMBuildCondBr(ir, LLVMBuildIsNull(ir, key, "key_is_null"), key_null, do_set);

  filterx_jit_ir_add_sequence_to_block(jit, key_null, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, key_null);
  fx_jit_emit_object_unref(jit, new_value);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, do_set, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, do_set);
  FilterXIRValue cloned = fx_jit_emit_object_cow_fork2(jit, new_value);
  FilterXIRValue object = filterx_expr_compile_or_eval_typed(self->object, jit);

  FilterXIRValue args[] = { object, key, cloned };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  LLVMBuildStore(ir, fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 3), result_slot);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, finish, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, finish);
  return LLVMBuildLoad2(ir, ffi->ptr_ty, result_slot, "result");
}

#endif

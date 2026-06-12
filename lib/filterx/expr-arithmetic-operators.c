/*
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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
#include "expr-arithmetic-operators.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-string-operators.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-eval.h"

#include <math.h>

/* If you want to make the arithmetic operators support other types,
 * follow the pattern of filterx_operator_plus_new() which uses
 * filterx_object_add() for type-generic addition. */

typedef struct FilterXArithmeticOperator_
{
  FilterXBinaryOp super;
  FilterXObject *literal_lhs;
  FilterXObject *literal_rhs;
} FilterXArithmeticOperator;


/* consumes operand objects */
static gboolean
_extract_operands_into_generic_numbers(FilterXObject *lhs_object, FilterXObject *rhs_object,
                                       GenericNumber *lhs_number, GenericNumber *rhs_number, FilterXExpr *expr)
{
  gboolean ok = FALSE;

  if (!filterx_object_extract_generic_number(lhs_object, lhs_number))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate arithmetic operator",
                                          "Left hand side must be a double or integer, got: %s",
                                          filterx_object_get_type_name(lhs_object));
      goto exit;
    }
  if (!filterx_object_extract_generic_number(rhs_object, rhs_number))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate arithmetic operator",
                                          "right hand side must be a double or integer, got: %s",
                                          filterx_object_get_type_name(rhs_object));
      goto exit;
    }
  ok = TRUE;

exit:
  filterx_object_unref(lhs_object);
  filterx_object_unref(rhs_object);
  return ok;
}

static inline FilterXObject *
_eval_lhs(FilterXArithmeticOperator *self)
{
  return self->literal_lhs ? filterx_object_ref(self->literal_lhs) : filterx_expr_eval_typed(self->super.lhs);
}

static inline FilterXObject *
_eval_rhs(FilterXArithmeticOperator *self)
{
  return self->literal_rhs ? filterx_object_ref(self->literal_rhs) : filterx_expr_eval(self->super.rhs);
}

static void
_optimize_arithmetic_operators_common(FilterXArithmeticOperator *self)
{
  if (filterx_expr_is_literal(self->super.lhs))
    self->literal_lhs = filterx_literal_get_value(self->super.lhs);

  if (filterx_expr_is_literal(self->super.rhs))
    self->literal_rhs = filterx_literal_get_value(self->super.rhs);
}

static void
_free_arithmetic_common(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  filterx_object_unref(self->literal_lhs);
  filterx_object_unref(self->literal_rhs);

  filterx_binary_op_free_method(s);
}

static FilterXObject *
_eval_op(FilterXArithmeticOperator *self,
         FilterXObject *(*op)(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr))
{
  FilterXObject *lhs, *rhs;
  lhs = _eval_lhs(self);
  if (!lhs)
    return NULL;
  rhs = _eval_rhs(self);
  if (!rhs)
    {
      filterx_object_unref(lhs);
      return NULL;
    }
  return (*op)(lhs, rhs, &self->super.super);
}

static FilterXObject *
_double_result(gdouble value)
{
  if (!isfinite(value))
    {
      filterx_eval_push_error_static_info("Failed to evaluate arithmetic operator",
                                          "Result is not a finite number");
      return NULL;
    }
  return filterx_double_new(value);
}

static FilterXObject *
_do_substraction(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  GenericNumber lhs_number, rhs_number, result;

  if (!_extract_operands_into_generic_numbers(lhs, rhs, &lhs_number, &rhs_number, expr))
    return NULL;

  if (lhs_number.type == GN_NAN || rhs_number.type == GN_NAN)
    return NULL;

  if (lhs_number.type == GN_INT64 && rhs_number.type == GN_INT64)
    {
      gint64 res;
      if (__builtin_sub_overflow(gn_as_int64(&lhs_number), gn_as_int64(&rhs_number), &res))
        {
          filterx_eval_push_error_static_info("Failed to evaluate subtraction operator", "Integer overflow");
          return NULL;
        }
      gn_set_int64(&result, res);
      return filterx_integer_new(gn_as_int64(&result));
    }

  return _double_result(gn_as_double(&lhs_number) - gn_as_double(&rhs_number));
}

static FilterXObject *
_eval_substraction(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _eval_op(self, _do_substraction);
}

static FilterXExpr *
_optimize_substraction(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    {
      FilterXObject *result = _eval_substraction(&self->super.super);
      if (result)
        return filterx_literal_new(result);
    }
  return NULL;
}

static FilterXObject *
_do_multiplication(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  GenericNumber lhs_number, rhs_number, result;

  if (!_extract_operands_into_generic_numbers(lhs, rhs, &lhs_number, &rhs_number, expr))
    return NULL;

  if (lhs_number.type == GN_NAN || rhs_number.type == GN_NAN)
    return NULL;

  if (lhs_number.type == GN_INT64 && rhs_number.type == GN_INT64)
    {
      gint64 res;
      if (__builtin_mul_overflow(gn_as_int64(&lhs_number), gn_as_int64(&rhs_number), &res))
        {
          filterx_eval_push_error_static_info("Failed to evaluate multiplication operator", "Integer overflow");
          return NULL;
        }
      gn_set_int64(&result, res);
      return filterx_integer_new(gn_as_int64(&result));
    }

  return _double_result(gn_as_double(&lhs_number) * gn_as_double(&rhs_number));
}

static FilterXObject *
_eval_multiplication(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _eval_op(self, _do_multiplication);
}

static FilterXExpr *
_optimize_multiplication(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    {
      FilterXObject *result = _eval_multiplication(&self->super.super);
      if (result)
        return filterx_literal_new(result);
    }
  return NULL;
}

static FilterXObject *
_do_division(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  GenericNumber lhs_number, rhs_number, result;

  if (!_extract_operands_into_generic_numbers(lhs, rhs, &lhs_number, &rhs_number, expr))
    return NULL;

  if (lhs_number.type == GN_NAN || rhs_number.type == GN_NAN)
    return NULL;

  if (lhs_number.type == GN_INT64 && rhs_number.type == GN_INT64)
    {
      gint64 lhs_int = gn_as_int64(&lhs_number);
      gint64 rhs_int = gn_as_int64(&rhs_number);

      if (rhs_int == 0)
        {
          filterx_eval_push_error_static_info("Failed to evaluate division operator", "Division by zero");
          return NULL;
        }
      if (lhs_int == G_MININT64 && rhs_int == -1)
        {
          filterx_eval_push_error_static_info("Failed to evaluate division operator",
                                              "Division overflow, INT64_MIN divided by -1");
          return NULL;
        }

      gn_set_int64(&result, lhs_int / rhs_int);
      return filterx_integer_new(gn_as_int64(&result));
    }

  return _double_result(gn_as_double(&lhs_number) / gn_as_double(&rhs_number));
}

static FilterXObject *
_eval_division(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _eval_op(self, _do_division);
}

static FilterXExpr *
_optimize_division(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    {
      FilterXObject *result = _eval_division(&self->super.super);
      if (result)
        return filterx_literal_new(result);
    }
  return NULL;
}

static FilterXObject *
_do_modulo(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  gint64 lhs_number, rhs_number;
  FilterXObject *result = NULL;

  if (!filterx_object_extract_integer(lhs, &lhs_number))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate modulo operator",
                                          "Left hand side must be an integer, got: %s",
                                          filterx_object_get_type_name(lhs));
      goto exit;
    }

  if (!filterx_object_extract_integer(rhs, &rhs_number))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate modulo operator",
                                          "Right hand side must be an integer, got: %s",
                                          filterx_object_get_type_name(rhs));
      goto exit;
    }

  if (rhs_number == 0)
    {
      filterx_eval_push_error_static_info("Failed to evaluate modulo operator", "Modulo by zero");
      goto exit;
    }
  if (lhs_number == G_MININT64 && rhs_number == -1)
    {
      filterx_eval_push_error_static_info("Failed to evaluate modulo operator",
                                          "Modulo overflow, INT64_MIN modulo -1");
      goto exit;
    }

  result = filterx_integer_new(lhs_number % rhs_number);

exit:
  filterx_object_unref(lhs);
  filterx_object_unref(rhs);
  return result;
}

static FilterXObject *
_eval_modulo(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _eval_op(self, _do_modulo);
}

static FilterXExpr *
_optimize_modulo(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    {
      FilterXObject *result = _eval_modulo(&self->super.super);
      if (result)
        return filterx_literal_new(result);
    }
  return NULL;
}

static FilterXObject *
_do_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  FilterXObject *result = filterx_object_add(lhs, rhs);

  filterx_object_unref(lhs);
  filterx_object_unref(rhs);
  return result;
}

static FilterXObject *
_eval_plus(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _eval_op(self, _do_plus);
}

static FilterXExpr *
_optimize_plus(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    {
      FilterXObject *result = _eval_plus(&self->super.super);
      if (result)
        return filterx_literal_new(result);
    }
  return NULL;
}

#if SYSLOG_NG_ENABLE_JIT
static inline gboolean
_is_numeric_static_type(FilterXStaticType kind)
{
  return kind == FILTERX_STATIC_TYPE_INTEGER || kind == FILTERX_STATIC_TYPE_DOUBLE;
}

static void
_infer_types_plus(FilterXExpr *s, FilterXTypeEnv *env)
{
  filterx_expr_infer_types_default(s, env);
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  FilterXStaticTypeSpec lhs_spec = self->super.lhs ? self->super.lhs->static_type : INITIAL_FILTERX_STATIC_TYPE_SPEC;
  FilterXStaticTypeSpec rhs_spec = self->super.rhs ? self->super.rhs->static_type : INITIAL_FILTERX_STATIC_TYPE_SPEC;
  FilterXStaticType lhs_kind = filterx_static_type_kind(lhs_spec);
  FilterXStaticType rhs_kind = filterx_static_type_kind(rhs_spec);

  /* Numeric promotion: filterx_object_add() keeps int+int in the integer domain but widens
   * to double as soon as either side is one, so a mixed pair is statically DOUBLE — a
   * result a plain meet would throw away as UNKNOWN.
   *
   * Both kinds must be known numerics for this to hold. An UNKNOWN operand is not a
   * "probably integer": it may be a double at runtime, which would widen the result, so the
   * pair stays UNKNOWN. Claiming INTEGER on a merely-unknown operand would let an
   * integer-speculating consumer treat a double as an int64. */
  if (_is_numeric_static_type(lhs_kind) && _is_numeric_static_type(rhs_kind))
    {
      FilterXStaticType result = (lhs_kind == FILTERX_STATIC_TYPE_DOUBLE || rhs_kind == FILTERX_STATIC_TYPE_DOUBLE)
                                 ? FILTERX_STATIC_TYPE_DOUBLE
                                 : FILTERX_STATIC_TYPE_INTEGER;
      s->static_type = filterx_static_type_kind_only(result);
      return;
    }

  s->static_type = filterx_static_type_spec_meet(lhs_spec, rhs_spec);
}

#endif

static FilterXObject *
_do_uminus(FilterXObject *operand_obj, FilterXExpr *expr)
{
  GenericNumber operand, result;
  FilterXObject *out = NULL;

  if (!operand_obj)
    {
      goto exit;
    }

  if (!filterx_object_extract_generic_number(operand_obj, &operand))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate arithmetic operator",
                                          "Operand must be a double or integer, got: %s",
                                          filterx_object_get_type_name(operand_obj));
      goto exit;
    }

  if (operand.type == GN_NAN)
    goto exit;

  if (operand.type == GN_INT64)
    {
      if (gn_as_int64(&operand) == G_MININT64)
        {
          filterx_eval_push_error_static_info("Failed to evaluate arithmetic operator",
                                              "Integer overflow, negation of INT64_MIN");
          goto exit;
        }
      gn_set_int64(&result, -gn_as_int64(&operand));
      out = filterx_integer_new(gn_as_int64(&result));
      goto exit;
    }

  out = _double_result(-gn_as_double(&operand));

exit:
  filterx_object_unref(operand_obj);
  return out;
}

static FilterXObject *
_eval_uminus(FilterXExpr *s)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;
  return _do_uminus(filterx_expr_eval_typed(self->operand), &self->super);
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

__attribute__((used))
FilterXObject *
fx_jit_arithmetic_sub(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  return _do_substraction(lhs, rhs, expr);
}

__attribute__((used))
FilterXObject *
fx_jit_arithmetic_mul(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  return _do_multiplication(lhs, rhs, expr);
}

__attribute__((used))
FilterXObject *
fx_jit_arithmetic_div(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  return _do_division(lhs, rhs, expr);
}

__attribute__((used))
FilterXObject *
fx_jit_arithmetic_mod(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  return _do_modulo(lhs, rhs, expr);
}

__attribute__((used))
FilterXObject *
fx_jit_arithmetic_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  return _do_plus(lhs, rhs, expr);
}

/* Devirtualized fast path for `integer + anything`. lhs is guaranteed to be a FilterXInteger
 * (eval_typed of an INTEGER-static_type operand).  If rhs is also integer, performs direct
 * integer arithmetic; otherwise falls back to the generic vtable-dispatching path.
 * The fallback means the function is always correct even when rhs is non-integer. */
__attribute__((used))
FilterXObject *
fx_jit_int_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  FilterXObject *result = NULL;
  if (!lhs)
    {
      filterx_eval_push_error_static_info("Failed to add values", "Failed to evaluate left hand side");
      goto exit;
    }
  if (!rhs)
    {
      filterx_eval_push_error_static_info("Failed to add values", "Failed to evaluate right hand side");
      goto exit;
    }

  gint64 rhs_val;
  if (G_LIKELY(filterx_integer_unwrap(rhs, &rhs_val)))
    {
      gint64 lhs_val;
      gboolean ok G_GNUC_UNUSED = filterx_integer_unwrap(lhs, &lhs_val);
      g_assert(ok);
      filterx_object_unref(lhs);
      filterx_object_unref(rhs);

      gint64 sum;
      if (__builtin_add_overflow(lhs_val, rhs_val, &sum))
        {
          /* filterx_object_add() rejects the same sums, see _integer_add() */
          filterx_eval_push_error_static_info("Failed to add values", "integer overflow");
          return NULL;
        }

      return filterx_integer_new(sum);
    }

  result = filterx_object_add(lhs, rhs);
  if (!result)
    filterx_eval_push_error_static_info("Failed to add values", "add() method failed");

exit:
  filterx_object_unref(lhs);
  filterx_object_unref(rhs);
  return result;
}

/* Unwraps a primitive number into a double, promoting an integer the way filterx_object_add()
 * does for a mixed pair. Returns FALSE for anything that is not a primitive number (a message
 * value for instance), leaving the caller to dispatch through the add() method instead. */
static inline gboolean
_unwrap_number_as_double(FilterXObject *obj, gdouble *value)
{
  if (filterx_double_unwrap(obj, value))
    return TRUE;

  gint64 int_value;
  if (filterx_integer_unwrap(obj, &int_value))
    {
      *value = (gdouble) int_value;
      return TRUE;
    }

  return FALSE;
}

/* Devirtualized fast path for a `+` inferred as DOUBLE, which _infer_types_plus() claims for
 * a statically numeric operand pair with at least one double. Which side holds the double is
 * not known at compile time, so both operands are unwrapped as numbers; either one that does
 * not unwrap directly falls back to the generic vtable-dispatching path, which means the
 * function stays correct regardless. */
__attribute__((used))
FilterXObject *
fx_jit_double_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  FilterXObject *result = NULL;
  if (!lhs)
    {
      filterx_eval_push_error_static_info("Failed to add values", "Failed to evaluate left hand side");
      goto exit;
    }
  if (!rhs)
    {
      filterx_eval_push_error_static_info("Failed to add values", "Failed to evaluate right hand side");
      goto exit;
    }

  gdouble lhs_val, rhs_val;
  if (G_LIKELY(_unwrap_number_as_double(lhs, &lhs_val) && _unwrap_number_as_double(rhs, &rhs_val)))
    {
      filterx_object_unref(lhs);
      filterx_object_unref(rhs);

      gdouble sum = lhs_val + rhs_val;
      if (!isfinite(sum))
        {
          /* filterx_object_add() rejects the same sums, see _double_add_result() */
          filterx_eval_push_error_static_info("Failed to add values", "double overflow");
          return NULL;
        }

      return filterx_double_new(sum);
    }

  result = filterx_object_add(lhs, rhs);
  if (!result)
    filterx_eval_push_error_static_info("Failed to add values", "add() method failed");

exit:
  filterx_object_unref(lhs);
  filterx_object_unref(rhs);
  return result;
}

__attribute__((used))
FilterXObject *
fx_jit_arithmetic_uminus(FilterXObject *operand, FilterXExpr *expr)
{
  return _do_uminus(operand, expr);
}

static FilterXIRValue
_compile_binary_arithmetic(FilterXExpr *s, FilterXJIT *jit, const gchar *fn_name)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);

  FilterXIRValue result_slot = LLVMBuildAlloca(ir, ffi->ptr_ty, "result");
  LLVMBuildStore(ir, LLVMConstNull(ffi->ptr_ty), result_slot);

  FilterXIRSequence lhs_null = filterx_jit_ir_create_sequence(jit, "arith_lhs_null", block);
  FilterXIRSequence eval_rhs = filterx_jit_ir_create_sequence(jit, "arith_eval_rhs", block);
  FilterXIRSequence rhs_null = filterx_jit_ir_create_sequence(jit, "arith_rhs_null", block);
  FilterXIRSequence do_op = filterx_jit_ir_create_sequence(jit, "arith_do_op", block);
  FilterXIRSequence finish = filterx_jit_ir_create_sequence(jit, "arith_finish", block);

  FilterXIRValue lhs = self->literal_lhs
                       ? fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->literal_lhs))
                       : filterx_expr_compile_or_eval_typed(self->super.lhs, jit);

  /* if (!lhs) goto finish; */
  FilterXIRValue lhs_is_null = LLVMBuildIsNull(ir, lhs, "lhs_is_null");
  LLVMBuildCondBr(ir, lhs_is_null, lhs_null, eval_rhs);

  filterx_jit_ir_add_sequence_to_block(jit, lhs_null, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, lhs_null);
  LLVMBuildBr(ir, finish);

  /* eval_rhs */
  filterx_jit_ir_add_sequence_to_block(jit, eval_rhs, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, eval_rhs);
  FilterXIRValue rhs = self->literal_rhs
                       ? fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->literal_rhs))
                       : filterx_expr_compile_or_eval(self->super.rhs, jit);

  /* if (!rhs) { unref(lhs); goto finish; } */
  FilterXIRValue rhs_is_null = LLVMBuildIsNull(ir, rhs, "rhs_is_null");
  LLVMBuildCondBr(ir, rhs_is_null, rhs_null, do_op);

  filterx_jit_ir_add_sequence_to_block(jit, rhs_null, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, rhs_null);
  fx_jit_emit_object_unref(jit, lhs);
  LLVMBuildBr(ir, finish);

  /* do_op: both operands are non-NULL; the called function consumes them */
  filterx_jit_ir_add_sequence_to_block(jit, do_op, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, do_op);
  FilterXIRValue args[] = { lhs, rhs, fx_jit_emit_const_ptr(jit, self) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  LLVMBuildStore(ir, fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 3), result_slot);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, finish, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, finish);
  return LLVMBuildLoad2(ir, ffi->ptr_ty, result_slot, "result");
}

/* Emits both operands, taking a ref on a literal instead of evaluating it. Mirrors
 * _eval_lhs()/_eval_rhs(): only the lhs is evaluated typed, which is what lets a
 * devirtualized fast path rely on its static type. */
static void
_compile_operands(FilterXArithmeticOperator *self, FilterXJIT *jit, FilterXIRValue *lhs, FilterXIRValue *rhs)
{
  *lhs = self->literal_lhs
         ? fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->literal_lhs))
         : filterx_expr_compile_or_eval_typed(self->super.lhs, jit);
  *rhs = self->literal_rhs
         ? fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->literal_rhs))
         : filterx_expr_compile_or_eval(self->super.rhs, jit);
}

static FilterXIRValue
_compile_plus(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  FilterXStaticType kind = filterx_static_type_kind(s->static_type);

  if (kind != FILTERX_STATIC_TYPE_STRING && !_is_numeric_static_type(kind))
    return _compile_binary_arithmetic(s, jit, "fx_jit_arithmetic_plus");

  FilterXIRValue lhs, rhs;
  _compile_operands(self, jit, &lhs, &rhs);

  if (kind == FILTERX_STATIC_TYPE_STRING)
    return filterx_string_concat_compile(jit, lhs, rhs, s);

  /* _infer_types_plus() claims a numeric kind only for a statically numeric operand pair:
   * INTEGER for int + int, DOUBLE as soon as either side is a double. Both domains have a
   * helper that adds without dispatching through the add() method. */
  const gchar *fn_name = kind == FILTERX_STATIC_TYPE_INTEGER ? "fx_jit_int_plus" : "fx_jit_double_plus";
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  FilterXIRValue args[] = { lhs, rhs, fx_jit_emit_const_ptr(jit, self) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  return fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 3);
}

static FilterXIRValue
_compile_substraction(FilterXExpr *s, FilterXJIT *jit)
{
  return _compile_binary_arithmetic(s, jit, "fx_jit_arithmetic_sub");
}

static FilterXIRValue
_compile_multiplication(FilterXExpr *s, FilterXJIT *jit)
{
  return _compile_binary_arithmetic(s, jit, "fx_jit_arithmetic_mul");
}

static FilterXIRValue
_compile_division(FilterXExpr *s, FilterXJIT *jit)
{
  return _compile_binary_arithmetic(s, jit, "fx_jit_arithmetic_div");
}

static FilterXIRValue
_compile_modulo(FilterXExpr *s, FilterXJIT *jit)
{
  return _compile_binary_arithmetic(s, jit, "fx_jit_arithmetic_mod");
}

static FilterXIRValue
_compile_uminus(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  FilterXIRValue operand = filterx_expr_compile_or_eval_typed(self->operand, jit);
  FilterXIRValue args[] = { operand, fx_jit_emit_const_ptr(jit, s) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty };
  return fx_jit_emit_extern_call(jit, "fx_jit_arithmetic_uminus", ffi->ptr_ty, param_tys, args, 2);
}

#endif

FilterXExpr *
filterx_operator_substraction_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "subs", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_substraction;
  self->super.super.eval = _eval_substraction;
  self->super.super.free_fn = _free_arithmetic_common;
#if SYSLOG_NG_ENABLE_JIT
  self->super.super.compile = _compile_substraction;
#endif

  return &self->super.super;
}

FilterXExpr *
filterx_operator_division_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "subs", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_division;
  self->super.super.eval = _eval_division;
  self->super.super.free_fn = _free_arithmetic_common;
#if SYSLOG_NG_ENABLE_JIT
  self->super.super.compile = _compile_division;
#endif

  return &self->super.super;
}

FilterXExpr *
filterx_operator_modulo_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "mod", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_modulo;
  self->super.super.eval = _eval_modulo;
  self->super.super.free_fn = _free_arithmetic_common;
#if SYSLOG_NG_ENABLE_JIT
  self->super.super.compile = _compile_modulo;
#endif

  return &self->super.super;
}

FilterXExpr *
filterx_operator_multiplication_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "mult", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_multiplication;
  self->super.super.eval = _eval_multiplication;
  self->super.super.free_fn = _free_arithmetic_common;
#if SYSLOG_NG_ENABLE_JIT
  self->super.super.compile = _compile_multiplication;
#endif

  return &self->super.super;
}

FilterXExpr *
filterx_operator_plus_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "plus", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_plus;
  self->super.super.eval = _eval_plus;
  self->super.super.free_fn = _free_arithmetic_common;
#if SYSLOG_NG_ENABLE_JIT
  self->super.super.compile = _compile_plus;
  self->super.super.infer_types = _infer_types_plus;
#endif

  return &self->super.super;
}

FilterXExpr *
filterx_operator_uminus_new(FilterXExpr *operand)
{
  FilterXUnaryOp *self = g_new0(FilterXUnaryOp, 1);
  filterx_unary_op_init_instance(self, "uminus", FXE_READ, operand);

  self->super.eval = _eval_uminus;
#if SYSLOG_NG_ENABLE_JIT
  self->super.compile = _compile_uminus;
#endif

  return &self->super;
}

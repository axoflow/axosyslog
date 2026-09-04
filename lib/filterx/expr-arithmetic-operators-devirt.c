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
#include "expr-arithmetic-operators-private.h"
#include "expr-arithmetic-operators-devirt.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-eval.h"

#include <math.h>

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

/* @lhs is a FilterXInteger, the eval_typed result of an INTEGER-static_type operand.
 * _compile_plus() guards it against NULL, only @rhs can still fail here. */
__attribute__((used))
FilterXObject *
fx_jit_int_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  FilterXObject *result = NULL;
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

/* Promotes an integer the way filterx_object_add() does for a mixed pair. */
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

/* Which side holds the double is not known at compile time, so both operands are unwrapped
 * as numbers. _compile_plus() guards @lhs against NULL, only @rhs can still fail here. */
__attribute__((used))
FilterXObject *
fx_jit_double_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  FilterXObject *result = NULL;
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

/* Mirrors _eval_op(): only the lhs is evaluated typed, which is what lets a fast path rely
 * on its static type, and the rhs is not evaluated when the lhs fails. */
static void
_compile_operands(FilterXArithmeticOperator *self, FilterXJIT *jit, FilterXIRShortCircuit *short_circuit,
                  FilterXIRValue *lhs, FilterXIRValue *rhs)
{
  *lhs = self->literal_lhs
         ? fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->literal_lhs))
         : filterx_expr_compile_or_eval_typed(self->super.lhs, jit);
  fx_jit_emit_bail_if_null(jit, short_circuit, *lhs, NULL);

  *rhs = self->literal_rhs
         ? fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->literal_rhs))
         : filterx_expr_compile_or_eval(self->super.rhs, jit);
}

static inline gboolean
_is_numeric_static_type(FilterXStaticType static_type)
{
  return static_type == FILTERX_STATIC_TYPE_INTEGER || static_type == FILTERX_STATIC_TYPE_DOUBLE;
}

FilterXIRValue
_compile_plus(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  FilterXStaticType kind = s->static_type;

  if (kind != FILTERX_STATIC_TYPE_STRING && !_is_numeric_static_type(kind))
    return _compile_binary_arithmetic(s, jit, "fx_jit_arithmetic_plus");

  FilterXIRShortCircuit short_circuit;
  fx_jit_emit_short_circuit_begin(jit, &short_circuit, "plus");

  FilterXIRValue lhs, rhs;
  _compile_operands(self, jit, &short_circuit, &lhs, &rhs);

  if (kind == FILTERX_STATIC_TYPE_STRING)
    return fx_jit_emit_short_circuit_end(jit, &short_circuit, filterx_string_concat_compile(jit, lhs, rhs, s));

  /* _infer_types_plus() claims INTEGER for int + int, DOUBLE as soon as either side is a
   * double. */
  const gchar *fn_name = kind == FILTERX_STATIC_TYPE_INTEGER ? "fx_jit_int_plus" : "fx_jit_double_plus";
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  FilterXIRValue args[] = { lhs, rhs, fx_jit_emit_const_ptr(jit, self) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  FilterXIRValue result = fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 3);

  return fx_jit_emit_short_circuit_end(jit, &short_circuit, result);
}

/* @lhs is a FilterXString, the eval_typed result of a STRING-static_type operand.
 * _compile_plus() guards it against NULL, only @rhs can still fail here. */
__attribute__((used))
FilterXObject *
fx_jit_string_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  FilterXObject *result = NULL;
  if (!rhs)
    {
      filterx_eval_push_error_static_info("Failed to add values", "Failed to evaluate right hand side");
      goto exit;
    }
  result = filterx_string_concat(lhs, rhs);
  if (!result)
    filterx_eval_push_error_static_info("Failed to add values", "add() method failed");

exit:
  filterx_object_unref(lhs);
  filterx_object_unref(rhs);
  return result;
}

FilterXIRValue
filterx_string_concat_compile(FilterXJIT *jit, FilterXIRValue lhs, FilterXIRValue rhs, FilterXExpr *expr)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  FilterXIRValue args[] = { lhs, rhs, fx_jit_emit_const_ptr(jit, expr) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  return fx_jit_emit_extern_call(jit, "fx_jit_string_plus", ffi->ptr_ty, param_tys, args, 3);
}

#endif

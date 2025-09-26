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

#include "filterx/expr-comparison.h"
#include "filterx/object-datetime.h"
#include "filterx/filterx-globals.h"
#include "filterx/object-extractor.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "filterx/object-string.h"
#include "filterx/object-datetime.h"
#include "filterx/object-message-value.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "object-primitive.h"
#include "generic-number.h"
#include "parse-number.h"
#include "scratch-buffers.h"

typedef struct _FilterXComparison
{
  FilterXBinaryOp super;
  gint operator;
  FilterXObject *literal_lhs;
  FilterXObject *literal_rhs;
} FilterXComparison;

static inline void
_convert_filterx_object_to_generic_number(FilterXObject *obj, GenericNumber *gn)
{
  if (filterx_object_extract_generic_number(obj, gn))
    return;

  const gchar *str;
  if (filterx_object_extract_string_ref(obj, &str, NULL))
    {
      if (!parse_generic_number(str, gn))
        gn_set_nan(gn);
      return;
    }

  if (filterx_object_extract_null(obj))
    {
      gn_set_int64(gn, 0);
      return;
    }

  UnixTime utime;
  if (filterx_object_extract_datetime(obj, &utime))
    {
      guint64 unix_epoch = unix_time_to_unix_epoch_usec(utime);
      gn_set_int64(gn, (gint64) MIN(unix_epoch, G_MAXINT64));
      return;
    }

  gn_set_nan(gn);
}

static inline const gchar *
_convert_filterx_object_to_string(FilterXObject *obj, gsize *len)
{
  const gchar *str;
  if (filterx_object_extract_string_ref(obj, &str, len) ||
      filterx_object_extract_bytes_ref(obj, &str, len) ||
      filterx_object_extract_protobuf_ref(obj, &str, len))
    {
      return str;
    }

  GString *buffer = scratch_buffers_alloc();
  LogMessageValueType lmvt;
  if (!filterx_object_marshal(obj, buffer, &lmvt))
    return NULL;

  *len = buffer->len;
  return buffer->str;
}

static inline gboolean
_evaluate_comparison(gint cmp, gint operator)
{
  gboolean result = FALSE;
  if (cmp == 0)
    {
      result = operator & FCMPX_EQ;
    }
  else if (cmp < 0)
    {
      result = !!(operator & FCMPX_LT);
    }
  else
    {
      result = !!(operator & FCMPX_GT);
    }
  return result;
}

static inline gboolean
_evaluate_as_string(FilterXObject *lhs, FilterXObject *rhs, gint operator)
{
  gsize lhs_len, rhs_len;
  const gchar *lhs_repr = _convert_filterx_object_to_string(lhs, &lhs_len);
  const gchar *rhs_repr = _convert_filterx_object_to_string(rhs, &rhs_len);

  gint result = memcmp(lhs_repr, rhs_repr, MIN(lhs_len, rhs_len));
  if (result == 0)
    result = lhs_len - rhs_len;
  return _evaluate_comparison(result, operator);
}

static inline gboolean
_evaluate_as_num(FilterXObject *lhs, FilterXObject *rhs, gint operator)
{
  GenericNumber lhs_number, rhs_number;
  _convert_filterx_object_to_generic_number(lhs, &lhs_number);
  _convert_filterx_object_to_generic_number(rhs, &rhs_number);

  if (gn_is_nan(&lhs_number) || gn_is_nan(&rhs_number))
    return operator == FCMPX_NE;
  return _evaluate_comparison(gn_compare(&lhs_number, &rhs_number), operator);
}

static gboolean
_evaluate_type_aware(FilterXObject *lhs, FilterXObject *rhs, gint operator)
{
#if SYSLOG_NG_ENABLE_DEBUG
  /* we already unwrapped the ref in filterx_compare_objects() by the time we got here */
  g_assert(!(filterx_object_is_ref(lhs) || filterx_object_is_ref(rhs)));
#endif

  if (lhs->type == rhs->type &&
      (filterx_object_is_type(lhs, &FILTERX_TYPE_NAME(string)) ||
       filterx_object_is_type(lhs, &FILTERX_TYPE_NAME(bytes)) ||
       filterx_object_is_type(lhs, &FILTERX_TYPE_NAME(protobuf))))
    return _evaluate_as_string(lhs, rhs, operator);

  /* FIXME: add dict/list comparison */

  if (filterx_object_is_type(lhs, &FILTERX_TYPE_NAME(null)) ||
      filterx_object_is_type(rhs, &FILTERX_TYPE_NAME(null))
     )
    {
      if (operator == FCMPX_NE)
        return lhs->type != rhs->type;
      if (operator == FCMPX_EQ)
        return lhs->type == rhs->type;
    }

  return _evaluate_as_num(lhs, rhs, operator);
}

static gboolean
_evaluate_type_and_value_based(FilterXObject *lhs, FilterXObject *rhs, gint operator)
{
#if SYSLOG_NG_ENABLE_DEBUG
  /* we already unwrapped the ref in filterx_compare_objects() by the time we got here */
  g_assert(!(filterx_object_is_ref(lhs) || filterx_object_is_ref(rhs)));
#endif

  switch (operator)
    {
    case FCMPX_EQ:
      if (lhs->type != rhs->type)
        return FALSE;
      break;
    case FCMPX_NE:
      if (lhs->type != rhs->type)
        return TRUE;
      break;
    default:
      g_assert_not_reached();
    }
  return _evaluate_type_aware(lhs, rhs, operator);
}

static inline FilterXObject *
_eval_based_on_compare_mode(FilterXExpr *expr, gint compare_mode)
{
  if (compare_mode & (FCMPX_TYPE_AWARE + FCMPX_TYPE_AND_VALUE_BASED))
    return filterx_expr_eval_typed(expr);
  return filterx_expr_eval(expr);
}

gboolean
filterx_compare_objects(FilterXObject *lhs, FilterXObject *rhs, gint cmp)
{
  gint op = cmp & FCMPX_OP_MASK;

  switch (cmp & FCMPX_MODE_MASK)
    {
    case FCMPX_TYPE_AWARE:
      return _evaluate_type_aware(lhs, rhs, op);
    case FCMPX_STRING_BASED:
      return _evaluate_as_string(lhs, rhs, op);
    case FCMPX_NUM_BASED:
      return _evaluate_as_num(lhs, rhs, op);
    case FCMPX_TYPE_AND_VALUE_BASED:
      return _evaluate_type_and_value_based(lhs, rhs, op);
    default:
      g_assert_not_reached();
    }
}

static inline gboolean
_eval_operand(FilterXComparison *self,
              FilterXObject *literal_operand, FilterXExpr *operand_expr,
              FilterXObject **ref, FilterXObject **borrowed)
{
  if (literal_operand)
    {
      *ref = NULL;
      *borrowed = literal_operand;
    }
  else
    {
      *ref = *borrowed = _eval_based_on_compare_mode(operand_expr, self->operator & FCMPX_MODE_MASK);
    }
  return *borrowed != NULL;
}

static FilterXObject *
_eval_comparison(FilterXExpr *s)
{
  FilterXComparison *self = (FilterXComparison *) s;
  FilterXObject *result_obj = NULL;

  FilterXObject *lhs_ref = NULL, *lhs;
  FilterXObject *rhs_ref = NULL, *rhs;

  if (!_eval_operand(self, self->literal_lhs, self->super.lhs, &lhs_ref, &lhs))
    {
      filterx_eval_push_error_static_info("Failed to compare values", s, "Failed to evaluate left hand side");
      goto exit;
    }

  if (!_eval_operand(self, self->literal_rhs, self->super.rhs, &rhs_ref, &rhs))
    {
      filterx_eval_push_error_static_info("Failed to compare values", s, "Failed to evaluate right hand side");
      goto exit;
    }

  gboolean result = filterx_compare_objects(filterx_ref_unwrap_ro(lhs), filterx_ref_unwrap_ro(rhs), self->operator);
  result_obj = filterx_boolean_new(result);

exit:
  filterx_object_unref(lhs_ref);
  filterx_object_unref(rhs_ref);
  return result_obj;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXComparison *self = (FilterXComparison *) s;

  g_assert(!filterx_binary_op_optimize_method(s));

  if (filterx_expr_is_literal(self->super.lhs))
    self->literal_lhs = filterx_literal_get_value(self->super.lhs);

  if (filterx_expr_is_literal(self->super.rhs))
    self->literal_rhs = filterx_literal_get_value(self->super.rhs);

  if (self->literal_lhs && self->literal_rhs)
    return filterx_literal_new(_eval_comparison(&self->super.super));

  return NULL;
}

static void
_filterx_comparison_free(FilterXExpr *s)
{
  FilterXComparison *self = (FilterXComparison *) s;
  filterx_object_unref(self->literal_lhs);
  filterx_object_unref(self->literal_rhs);

  filterx_binary_op_free_method(s);
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_comparison_new(FilterXExpr *lhs, FilterXExpr *rhs, gint operator)
{
  FilterXComparison *self = g_new0(FilterXComparison, 1);

  filterx_binary_op_init_instance(&self->super, "comparison", FALSE, lhs, rhs);
  self->super.super.optimize = _optimize;
  self->super.super.eval = _eval_comparison;
  self->super.super.free_fn = _filterx_comparison_free;
  self->operator = operator;

  return &self->super.super;
}

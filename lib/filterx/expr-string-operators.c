/*
 * Copyright (c) 2025 László Várady
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

#include "expr-string-operators.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"


typedef struct FilterXSlicingOperator_
{
  FilterXExpr super;
  FilterXExpr *lhs;

  FilterXExpr *start;
  FilterXObject *start_literal;

  FilterXExpr *end;
  FilterXObject *end_literal;
} FilterXSlicingOperator;

static inline gint64
_clamp_index(gint64 idx, gssize len)
{
  if (idx < 0)
    return 0;

  if (idx > len)
    return len;

  return idx;
}

static FilterXObject *
_str_slice(FilterXSlicingOperator *self, const gchar *str, gsize str_len, gint64 start, gint64 end)
{
  gssize len = (gssize) str_len;

  if (start < 0)
    start = len + start;

  if (end < 0)
    end = len + end;

  start = _clamp_index(start, len);
  end = _clamp_index(end, len);

  if (start > end)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate string slicing operator", &self->super,
                                          "Start index must not be greater than end index");
      return NULL;
    }

  return filterx_string_new(str + start, end - start);
}

static gboolean
_extract_start(FilterXSlicingOperator *self, gint64 *start_idx)
{
  if (!self->start)
    {
      *start_idx = 0;
      return TRUE;
    }

  FilterXObject *start = self->start_literal ? filterx_object_ref(self->start_literal) : filterx_expr_eval(self->start);
  if (!start)
    {
      filterx_eval_push_error_static_info("Failed to evaluate string slicing operator", &self->super,
                                          "Failed to evaluate start index");
      return FALSE;
    }

  if (!filterx_object_extract_integer(start, start_idx))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate string slicing operator", &self->super,
                                          "Start index must be an integer, got: %s",
                                          filterx_object_get_type_name(start));

      filterx_object_unref(start);
      return FALSE;
    }

  filterx_object_unref(start);
  return TRUE;
}

static gboolean
_extract_end(FilterXSlicingOperator *self, gsize str_len, gint64 *end_idx)
{
  if (!self->end)
    {
      *end_idx = str_len;
      return TRUE;
    }

  FilterXObject *end = self->end_literal ? filterx_object_ref(self->end_literal) : filterx_expr_eval(self->end);
  if (!end)
    {
      filterx_eval_push_error_static_info("Failed to evaluate string slicing operator", &self->super,
                                          "Failed to evaluate end index");
      return FALSE;
    }

  if (!filterx_object_extract_integer(end, end_idx))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate string slicing operator", &self->super,
                                          "End index must be an integer, got: %s",
                                          filterx_object_get_type_name(end));
      filterx_object_unref(end);
      return FALSE;
    }

  filterx_object_unref(end);
  return TRUE;
}

static FilterXObject *
filterx_string_slicing_eval(FilterXExpr *s)
{
  FilterXSlicingOperator *self = (FilterXSlicingOperator *) s;

  FilterXObject *lhs_object = filterx_expr_eval_typed(self->lhs);
  if (!lhs_object)
    {
      filterx_eval_push_error_static_info("Failed to evaluate string slicing operator", &self->super,
                                          "Failed to evaluate left hand side");
      return NULL;
    }

  gsize str_len = 0;
  const gchar *str = filterx_string_get_value_ref(lhs_object, &str_len);
  if (!str)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate string slicing operator", &self->super,
                                          "Left hand side must be a string, got: %s",
                                          filterx_object_get_type_name(lhs_object));
      filterx_object_unref(lhs_object);
      return NULL;
    }


  gint64 start_idx = 0, end_idx = 0;

  if (!_extract_start(self, &start_idx))
    {
      filterx_object_unref(lhs_object);
      return NULL;
    }

  if (!_extract_end(self, str_len, &end_idx))
    {
      filterx_object_unref(lhs_object);
      return NULL;
    }

  FilterXObject *result = _str_slice(self, str, str_len, start_idx, end_idx);
  filterx_object_unref(lhs_object);
  return result;
}

void
filterx_string_slicing_free(FilterXExpr *s)
{
  FilterXSlicingOperator *self = (FilterXSlicingOperator *) s;

  filterx_expr_unref(self->lhs);
  filterx_expr_unref(self->start);
  filterx_expr_unref(self->end);
  filterx_object_unref(self->start_literal);
  filterx_object_unref(self->end_literal);
  filterx_expr_free_method(s);
}

FilterXExpr *
filterx_string_slicing_optimize(FilterXExpr *s)
{
  FilterXSlicingOperator *self = (FilterXSlicingOperator *) s;

  self->lhs = filterx_expr_optimize(self->lhs);
  self->start = filterx_expr_optimize(self->start);
  self->end = filterx_expr_optimize(self->end);

  if (filterx_expr_is_literal(self->start))
    self->start_literal = filterx_literal_get_value(self->start);

  if (filterx_expr_is_literal(self->end))
    self->end_literal = filterx_literal_get_value(self->end);

  return NULL;
}

static gboolean
filterx_string_slicing_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXSlicingOperator *self = (FilterXSlicingOperator *) s;

  FilterXExpr **exprs[] = { &self->lhs, &self->start, &self->end };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_string_slicing_new(FilterXExpr *lhs, FilterXExpr *start, FilterXExpr *end)
{
  FilterXSlicingOperator *self = g_new0(FilterXSlicingOperator, 1);

  filterx_expr_init_instance(&self->super, "string_slicing");
  self->super.optimize = filterx_string_slicing_optimize;
  self->super.walk_children = filterx_string_slicing_walk;
  self->super.free_fn = filterx_string_slicing_free;

  self->super.eval = filterx_string_slicing_eval;

  g_assert(lhs);
  g_assert(start || end);

  self->lhs = lhs;
  self->start = start;
  self->end = end;

  return &self->super;
}

/*
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
 * Copyright (c) 2025 Axoflow
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include "expr-switch.h"
#include "expr-compound.h"
#include "expr-comparison.h"
#include "expr-literal.h"
#include "object-string.h"
#include "object-primitive.h"

typedef struct _FilterXSwitch FilterXSwitch;
typedef struct _FilterXSwitchCase FilterXSwitchCase;

struct _FilterXSwitchCase
{
  FilterXUnaryOp super;
  gsize target;
};

static inline gboolean
filterx_switch_case_is_default(FilterXSwitchCase *self)
{
  return self->super.operand == NULL;
}

static void
filterx_switch_case_set_target(FilterXSwitchCase *self, gsize target)
{
  self->target = target;
}

static inline gsize
filterx_switch_case_get_target(FilterXSwitchCase *self)
{
  return self->target;
}

static inline FilterXObject *
_eval_switch_case(FilterXSwitchCase *self)
{
  return filterx_expr_eval_typed(self->super.operand);
}

FilterXExpr *
filterx_switch_case_new(FilterXExpr *value)
{
  FilterXSwitchCase *self = g_new0(FilterXSwitchCase, 1);
  filterx_unary_op_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(switch_case), value);
  return &self->super.super;
}

struct _FilterXSwitch
{
  FilterXExpr super;
  FilterXExpr *selector;
  FilterXObject *evaluated_selector;
  /* pointer to FilterXSwitchCase instances */
  GPtrArray *cases;
  /* compound expr */
  FilterXExpr *body;
  gsize default_target;
  GHashTable *literal_cache;
  /* temporary init time error message storage */
  GString *_caching_error_msg;
};

static void
_store_duplicate_cases_error(FilterXSwitch *self, const gchar *str)
{
  if (!self->_caching_error_msg)
    self->_caching_error_msg = g_string_new("Duplicate cases detected: ");
  else
    g_string_append(self->_caching_error_msg, ", ");
  g_string_append(self->_caching_error_msg, str);
}

static gboolean
_try_to_cache_literal_switch_case(FilterXSwitch *self, FilterXExpr *switch_case_expr)
{
  FilterXSwitchCase *switch_case = (FilterXSwitchCase *) switch_case_expr;

  if (!filterx_expr_is_literal(switch_case->super.operand))
    return FALSE;

  FilterXObject *case_value = filterx_expr_eval(switch_case->super.operand);
  if (!case_value)
    return FALSE;

  gsize len;
  const gchar *str = filterx_string_get_value_ref(case_value, &len);
  if (!str)
    {
      filterx_object_unref(case_value);
      return FALSE;
    }

  if (!g_hash_table_insert(self->literal_cache, g_strdup(str), filterx_expr_ref(&switch_case->super.super)))
    {
      /* Switch case already exists, this is not allowed. */
      _store_duplicate_cases_error(self, str);
    }

  filterx_object_unref(case_value);
  return TRUE;
}

static void
_build_switch_table(FilterXSwitch *self, GList *body)
{
  self->body = filterx_compound_expr_new(FALSE);
  for (GList *l = body; l; l = l->next)
    {
      FilterXExpr *expr = (FilterXExpr *) l->data;

      if (filterx_expr_is_switch_case(expr))
        {
          FilterXSwitchCase *switch_case = (FilterXSwitchCase *) expr;

          if (!filterx_switch_case_is_default(switch_case))
            {
              filterx_switch_case_set_target(switch_case, filterx_compound_expr_get_count(self->body));
              g_ptr_array_add(self->cases, expr);
            }
          else
            {
              /* we do not add the default to our cases */
              if (self->default_target != -1)
                {
                  /* Default case already set, this is not allowed. */
                  _store_duplicate_cases_error(self, "default");
                }
              self->default_target = filterx_compound_expr_get_count(self->body);
              filterx_expr_unref(expr);
            }
        }
      else
        filterx_compound_expr_add(self->body, expr);
    }
  g_list_free(body);
}

static FilterXObject *
_eval_body(FilterXSwitch *self, gssize target)
{
  if (target < 0)
    return filterx_boolean_new(TRUE);

  return filterx_compound_expr_eval_ext(self->body, target);
}

static FilterXSwitchCase *
_find_matching_literal_case(FilterXSwitch *self, FilterXObject *selector)
{
  gsize len;
  const gchar *str = filterx_string_get_value_ref(selector, &len);
  if (!str)
    return NULL;

  return g_hash_table_lookup(self->literal_cache, str);
}

static FilterXSwitchCase *
_find_matching_case(FilterXSwitch *self, FilterXObject *selector)
{
  for (gsize i = 0; i < self->cases->len; i++)
    {
      FilterXSwitchCase *switch_case = (FilterXSwitchCase *) g_ptr_array_index(self->cases, i);

      FilterXObject *value = _eval_switch_case(switch_case);

      if (filterx_compare_objects(selector, value, FCMPX_TYPE_AND_VALUE_BASED | FCMPX_EQ))
        {
          filterx_object_unref(value);
          return switch_case;
        }

      filterx_object_unref(value);
    }
  return NULL;
}

static FilterXObject *
_eval_switch(FilterXExpr *s)
{
  FilterXSwitch *self = (FilterXSwitch *) s;
  FilterXObject *selector = filterx_expr_eval_typed(self->selector);

  FilterXSwitchCase *switch_case;

  switch_case = _find_matching_literal_case(self, selector);
  if (!switch_case)
    switch_case = _find_matching_case(self, selector);

  gssize target = -1;
  if (switch_case)
    target = filterx_switch_case_get_target(switch_case);

  if (target < 0)
    target = self->default_target;

  FilterXObject *result = _eval_body(self, target);
  filterx_object_unref(selector);
  return result;
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  if (self->_caching_error_msg)
    {
      msg_error("Error during switch-case initialization",
                evt_tag_str("error", self->_caching_error_msg->str),
                filterx_expr_format_location_tag(s));
      return FALSE;
    }

  if (!filterx_expr_init(self->selector, cfg))
    goto error;

  if (!filterx_expr_init(self->body, cfg))
    goto error;

  for (gsize i = 0; i < self->cases->len; i++)
    {
      FilterXExpr *expr = (FilterXExpr *) g_ptr_array_index(self->cases, i);
      if (!filterx_expr_init(expr, cfg))
        goto error;
    }

  return filterx_expr_init_method(s, cfg);

error:
  for (gsize i = 0; i < self->cases->len; i++)
    {
      FilterXExpr *expr = (FilterXExpr *) g_ptr_array_index(self->cases, i);
      filterx_expr_deinit(expr, cfg);
    }
  filterx_expr_deinit(self->body, cfg);
  filterx_expr_deinit(self->selector, cfg);
  return FALSE;
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  for (gsize i = 0; i < self->cases->len; i++)
    {
      FilterXExpr *expr = (FilterXExpr *) g_ptr_array_index(self->cases, i);
      filterx_expr_deinit(expr, cfg);
    }
  filterx_expr_deinit(self->body, cfg);
  filterx_expr_deinit(self->selector, cfg);
  filterx_expr_deinit_method(s, cfg);
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  self->body = filterx_expr_optimize(self->body);
  self->selector = filterx_expr_optimize(self->selector);

  for (gssize i = (gssize)(self->cases->len) - 1; i >= 0; i--)
    {
      FilterXExpr *switch_case = (FilterXExpr *) g_ptr_array_index(self->cases, i);
      FilterXExpr *optimized_switch_case = filterx_expr_optimize(switch_case);
      g_ptr_array_index(self->cases, i) = optimized_switch_case;

      if (_try_to_cache_literal_switch_case(self, optimized_switch_case))
        g_ptr_array_remove_index(self->cases, i);
    }

  return NULL;
}

static void
_free(FilterXExpr *s)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  filterx_expr_unref(self->body);
  filterx_expr_unref(self->selector);
  g_ptr_array_free(self->cases, TRUE);
  g_hash_table_unref(self->literal_cache);
  if (self->_caching_error_msg)
    g_string_free(self->_caching_error_msg, TRUE);
  filterx_expr_free_method(s);
}

FilterXExpr *
filterx_switch_new(FilterXExpr *selector, GList *body)
{
  FilterXSwitch *self = g_new0(FilterXSwitch, 1);

  filterx_expr_init_instance(&self->super, "switch");
  self->super.init = _init;
  self->super.deinit = _deinit;
  self->super.optimize = _optimize;
  self->super.eval = _eval_switch;
  self->super.free_fn = _free;
  self->cases = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_expr_unref);
  self->literal_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) filterx_expr_unref);
  self->selector = selector;
  self->default_target = -1;
  _build_switch_table(self, body);
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(switch);
FILTERX_EXPR_DEFINE_TYPE(switch_case);

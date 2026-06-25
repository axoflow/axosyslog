/*
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
 * Copyright (c) 2025 Axoflow
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
#include "expr-switch.h"
#include "expr-compound.h"
#include "expr-comparison.h"
#include "expr-literal.h"
#include "expr-function.h"
#include "object-string.h"
#include "object-primitive.h"
#include "filterx-eval.h"

typedef struct _FilterXSwitch FilterXSwitch;
typedef struct _FilterXSwitchCase FilterXSwitchCase;

struct _FilterXSwitchCase
{
  FilterXExpr super;
  gboolean (*match)(FilterXSwitchCase *self, FilterXObject *selector, GError **error);
  gsize target;
};

typedef struct _FilterXSwitchCaseSingle
{
  FilterXSwitchCase super;
  FilterXExpr *value;   /* NULL for the default case */
} FilterXSwitchCaseSingle;

typedef struct _FilterXSwitchCaseRange
{
  FilterXSwitchCase super;
  FilterXExpr *lower;
  FilterXExpr *upper;
} FilterXSwitchCaseRange;

static inline gboolean
filterx_switch_case_is_default(FilterXSwitchCase *self)
{
  return self->match == NULL;
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

static gboolean
_switch_case_single_match(FilterXSwitchCase *s, FilterXObject *selector, GError **error)
{
  FilterXSwitchCaseSingle *self = (FilterXSwitchCaseSingle *) s;
  FilterXObject *value = filterx_expr_eval_typed(self->value);
  if (!value)
    return FALSE;
  gboolean result = filterx_compare_objects(selector, value, FCMPX_TYPE_AND_VALUE_BASED | FCMPX_EQ);
  filterx_object_unref(value);
  return result;
}

static void
_switch_case_single_free(FilterXExpr *s)
{
  FilterXSwitchCaseSingle *self = (FilterXSwitchCaseSingle *) s;
  filterx_expr_unref(self->value);
  filterx_expr_free_method(s);
}

static gboolean
_switch_case_single_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXSwitchCaseSingle *self = (FilterXSwitchCaseSingle *) s;
  if (self->value)
    return filterx_expr_visit(s, &self->value, f, user_data);
  return TRUE;
}

static gboolean
_switch_case_range_match(FilterXSwitchCase *s, FilterXObject *selector, GError **error)
{

  if (!(filterx_object_is_type(selector, &FILTERX_TYPE_NAME(integer))
        || filterx_object_is_type(selector, &FILTERX_TYPE_NAME(double))))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Failed to evaluate range case operator. Selector value must be a double or integer, got: %s",
                  filterx_object_get_type_name(selector));
      return FALSE;
    }

  FilterXSwitchCaseRange *self = (FilterXSwitchCaseRange *) s;
  FilterXObject *lower = filterx_expr_eval_typed(self->lower);
  if (!lower)
    return FALSE;
  if (!(filterx_object_is_type(lower, &FILTERX_TYPE_NAME(integer))
        || filterx_object_is_type(lower, &FILTERX_TYPE_NAME(double))))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Failed to evaluate range case operator. Lower value must be a double or integer, got: %s",
                  filterx_object_get_type_name(lower));
      filterx_object_unref(lower);
      return FALSE;
    }
  FilterXObject *upper = filterx_expr_eval_typed(self->upper);
  if (!upper)
    {
      filterx_object_unref(lower);
      return FALSE;
    }
  if (!(filterx_object_is_type(upper, &FILTERX_TYPE_NAME(integer))
        || filterx_object_is_type(upper, &FILTERX_TYPE_NAME(double))))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "Failed to evaluate range case operator. Upper value must be a double or integer, got: %s",
                  filterx_object_get_type_name(upper));
      filterx_object_unref(lower);
      filterx_object_unref(upper);
      return FALSE;
    }
  gboolean result = FALSE;
  if (filterx_compare_objects(lower, upper, FCMPX_NUM_BASED | FCMPX_LT))
    result = filterx_compare_objects(lower, selector, FCMPX_NUM_BASED | FCMPX_LT | FCMPX_EQ) &&
             filterx_compare_objects(selector, upper, FCMPX_NUM_BASED | FCMPX_LT);
  else
    g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                "Failed to evaluate range case operator. Upper boundary has to be greater than lower: %s",
                filterx_object_get_type_name(upper));
  filterx_object_unref(lower);
  filterx_object_unref(upper);
  return result;
}

static void
_switch_case_range_free(FilterXExpr *s)
{
  FilterXSwitchCaseRange *self = (FilterXSwitchCaseRange *) s;
  filterx_expr_unref(self->lower);
  filterx_expr_unref(self->upper);
  filterx_expr_free_method(s);
}

static gboolean
_switch_case_range_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXSwitchCaseRange *self = (FilterXSwitchCaseRange *) s;
  if (!filterx_expr_visit(s, &self->lower, f, user_data))
    return FALSE;
  return filterx_expr_visit(s, &self->upper, f, user_data);
}

FilterXExpr *
filterx_switch_case_new(FilterXExpr *value)
{
  FilterXSwitchCaseSingle *self = g_new0(FilterXSwitchCaseSingle, 1);
  filterx_expr_init_instance(&self->super.super, FILTERX_EXPR_TYPE_NAME(switch_case), FXE_READ);
  self->super.super.free_fn = _switch_case_single_free;
  self->super.super.walk_children = _switch_case_single_walk;
  self->super.match = value ? _switch_case_single_match : NULL;
  self->super.target = -1;
  self->value = value;
  return &self->super.super;
}

FilterXExpr *
filterx_switch_case_range_new(FilterXExpr *lower, FilterXExpr *upper)
{
  FilterXSwitchCaseRange *self = g_new0(FilterXSwitchCaseRange, 1);
  filterx_expr_init_instance(&self->super.super, FILTERX_EXPR_TYPE_NAME(switch_case), FXE_READ);
  self->super.super.free_fn = _switch_case_range_free;
  self->super.super.walk_children = _switch_case_range_walk;
  self->super.match = _switch_case_range_match;
  self->super.target = -1;
  self->lower = lower;
  self->upper = upper;
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

  /* Only single-value (non-default, non-range) cases can be hash-cached */
  if (switch_case->match != _switch_case_single_match)
    return FALSE;

  FilterXSwitchCaseSingle *single = (FilterXSwitchCaseSingle *) switch_case;
  if (!filterx_expr_is_literal(single->value))
    return FALSE;

  FilterXObject *case_value = filterx_literal_get_value(single->value);
  if (!case_value)
    return FALSE;

  if (!filterx_object_is_type(case_value, &FILTERX_TYPE_NAME(string)))
    {
      filterx_object_unref(case_value);
      return FALSE;
    }

  /* NOTE: g_hash_table_insert() frees the key if it was a duplicate */
  if (!g_hash_table_insert(self->literal_cache, filterx_object_ref(case_value), filterx_expr_ref(&switch_case->super)))
    {
      /* Switch case already exists, this is not allowed. */
      _store_duplicate_cases_error(self, filterx_string_get_value_as_cstr(case_value));
    }

  filterx_object_unref(case_value);
  return TRUE;
}

/* Takes reference of body */
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
        filterx_compound_expr_add_ref(self->body, expr);
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

static inline FilterXSwitchCase *
_find_matching_literal_case(FilterXSwitch *self, FilterXObject *selector)
{
  if (!filterx_object_is_type(selector, &FILTERX_TYPE_NAME(string)))
    return NULL;

  return g_hash_table_lookup(self->literal_cache, selector);
}

static FilterXSwitchCase *
_find_matching_case(FilterXSwitch *self, FilterXObject *selector, GError **error)
{
  for (gsize i = 0; i < self->cases->len; i++)
    {
      FilterXSwitchCase *switch_case = (FilterXSwitchCase *) g_ptr_array_index(self->cases, i);
      if (switch_case->match(switch_case, selector, error))
        return switch_case;
      if (*error)
        return NULL;
    }
  return NULL;
}

static FilterXObject *
_eval_switch(FilterXExpr *s)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  FilterXObject *selector = filterx_expr_eval_typed(self->selector);
  if (!selector)
    {
      filterx_eval_push_error_static_info("Failed to evaluate switch", &self->super, "Failed to evaluate selector");
      return NULL;
    }

  FilterXSwitchCase *switch_case;
  GError *error = NULL;

  switch_case = _find_matching_literal_case(self, selector);
  if (!switch_case)
    switch_case = _find_matching_case(self, selector, &error);

  if (error)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate switch", &self->super, "%s", error->message);
      g_clear_error(&error);
      filterx_object_unref(selector);
      return NULL;
    }

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
      filterx_eval_push_error_info_printf("Failed to initialize switch-case", s, "%s", self->_caching_error_msg->str);
      return FALSE;
    }

  return filterx_expr_init_method(s, cfg);
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  for (gssize i = (gssize)(self->cases->len) - 1; i >= 0; i--)
    {
      FilterXExpr *switch_case = (FilterXExpr *) g_ptr_array_index(self->cases, i);

      if (_try_to_cache_literal_switch_case(self, switch_case))
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

static gboolean
_switch_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  if (self->selector)
    {
      if (!filterx_expr_visit(s, &self->selector, f, user_data))
        return FALSE;
    }

  if (self->body)
    {
      if (!filterx_expr_visit(s, &self->body, f, user_data))
        return FALSE;
    }

  for (gsize i = 0; i < self->cases->len; i++)
    {
      FilterXExpr **expr = (FilterXExpr **) &g_ptr_array_index(self->cases, i);
      if (!filterx_expr_visit(s, expr, f, user_data))
        return FALSE;
    }

  return TRUE;
}

static void
_switch_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  filterx_expr_infer_types(self->selector, env);

  /* Case-value expressions are read-only in practice but visit them so any nested
   * variable reads pick up the env. */
  for (gsize i = 0; i < self->cases->len; i++)
    {
      FilterXExpr *case_expr = (FilterXExpr *) g_ptr_array_index(self->cases, i);
      filterx_expr_infer_types(case_expr, env);
    }

  /* The body may or may not execute (no matching case + no default). Walk it on a clone
   * and meet back, so any writes the body performs are only kept if they would also hold
   * under the no-match path. Cross-case fall-through pessimizes the within-body view too,
   * which is fine for v1. */
  FilterXTypeEnv *before = filterx_type_env_clone(env);
  filterx_expr_infer_types(self->body, env);
  filterx_type_env_meet_into(env, before);
  filterx_type_env_free(before);

  s->static_type = INITIAL_FILTERX_STATIC_TYPE_SPEC;
}

FilterXExpr *
filterx_switch_new(FilterXExpr *selector, GList *body)
{
  FilterXSwitch *self = g_new0(FilterXSwitch, 1);

  filterx_expr_init_instance(&self->super, "switch", FXE_READ);
  self->super.init = _init;
  self->super.optimize = _optimize;
  self->super.eval = _eval_switch;
  self->super.walk_children = _switch_walk;
  self->super.free_fn = _free;
  self->super.infer_types = _switch_infer_types;
  self->cases = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_expr_unref);
  self->literal_cache = g_hash_table_new_full((GHashFunc) filterx_object_hash, (GEqualFunc) filterx_object_equal,
                                              (GDestroyNotify) filterx_object_unref, (GDestroyNotify) filterx_expr_unref);
  self->selector = selector;
  self->default_target = -1;
  _build_switch_table(self, body);
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(switch);
FILTERX_EXPR_DEFINE_TYPE(switch_case);

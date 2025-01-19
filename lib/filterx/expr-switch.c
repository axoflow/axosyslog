#include "expr-switch.h"
#include "expr-compound.h"
#include "expr-comparison.h"
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
};

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

static FilterXObject *
_eval_switch(FilterXExpr *s)
{
  FilterXSwitch *self = (FilterXSwitch *) s;
  FilterXObject *selector = filterx_expr_eval_typed(self->selector);

  gssize target = -1;
  for (gsize i = 0; i < self->cases->len; i++)
    {
      FilterXSwitchCase *switch_case = (FilterXSwitchCase *) g_ptr_array_index(self->cases, i);

      FilterXObject *value = _eval_switch_case(switch_case);
      if (filterx_compare_objects(selector, value, FCMPX_TYPE_AND_VALUE_BASED | FCMPX_EQ))
        target = filterx_switch_case_get_target(switch_case);
      filterx_object_unref(value);
    }
  if (target < 0)
    target = self->default_target;

  FilterXObject *result = _eval_body(self, target);
  filterx_object_unref(selector);
  return result;
}

static void
_free(FilterXExpr *s)
{
  FilterXSwitch *self = (FilterXSwitch *) s;

  filterx_expr_unref(self->body);
  filterx_expr_unref(self->selector);
  g_ptr_array_free(self->cases, TRUE);
  filterx_expr_free_method(s);
}

FilterXExpr *
filterx_switch_new(FilterXExpr *selector, GList *body)
{
  FilterXSwitch *self = g_new0(FilterXSwitch, 1);

  filterx_expr_init_instance(&self->super, "switch");
  self->super.eval = _eval_switch;
  self->super.free_fn = _free;
  self->cases = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_expr_unref);
  self->selector = selector;
  _build_switch_table(self, body);
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(switch);
FILTERX_EXPR_DEFINE_TYPE(switch_case);
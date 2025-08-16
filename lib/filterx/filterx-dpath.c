/*
 * Copyright (c) 2025 Axoflow
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

#include "filterx/filterx-dpath.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object.h"
#include "filterx/object-dict.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"

typedef enum _FilterXDPathElementType
{
  FILTERX_DPATH_ELEMENT_OBJECT,
  FILTERX_DPATH_ELEMENT_EXPR,
} FilterXDPathElementType;

struct _FilterXDPathElement
{
  FilterXDPathElementType type;

  union
  {
    FilterXObject *object;
    FilterXExpr *expr;
  };
};

typedef struct _FilterXDPathLValue
{
  FilterXExpr super;
  FilterXExpr *variable;
  GList *dpath_elements;

  gboolean append_mode;
} FilterXDPathLValue;

FilterXDPathElement *
filterx_dpath_elem_object_new(FilterXObject *object)
{
  FilterXDPathElement *self = g_new0(FilterXDPathElement, 1);
  self->type = FILTERX_DPATH_ELEMENT_OBJECT;
  self->object = object;
  return self;
}

FilterXDPathElement *
filterx_dpath_elem_expr_new(FilterXExpr *expr)
{
  FilterXDPathElement *self = g_new0(FilterXDPathElement, 1);
  self->type = FILTERX_DPATH_ELEMENT_EXPR;
  self->expr = expr;
  return self;
}

static void
filterx_dpath_elem_free(FilterXDPathElement *elem)
{
  if (elem->type == FILTERX_DPATH_ELEMENT_OBJECT)
    filterx_object_unref(elem->object);
  else if (elem->type == FILTERX_DPATH_ELEMENT_EXPR)
    filterx_expr_unref(elem->expr);

  g_free(elem);
}

static inline FilterXObject *
filterx_dpath_elem_get(FilterXObject *dict, FilterXDPathElement *elem, gboolean *error)
{
  if (elem->type == FILTERX_DPATH_ELEMENT_OBJECT)
    return filterx_object_getattr(dict, elem->object);
  else if (elem->type == FILTERX_DPATH_ELEMENT_EXPR)
    {
      FilterXObject *e = filterx_expr_eval(elem->expr);
      if (!e)
        {
          *error = TRUE;
          return NULL;
        }

      FilterXObject *obj = filterx_object_get_subscript(dict, e);
      filterx_object_unref(e);
      return obj;
    }

  g_assert_not_reached();
}

static inline gboolean
filterx_dpath_elem_set(FilterXObject *dict, FilterXDPathElement *elem, FilterXObject *value)
{
  if (elem->type == FILTERX_DPATH_ELEMENT_OBJECT)
    return filterx_object_setattr(dict, elem->object, &value);
  else if (elem->type == FILTERX_DPATH_ELEMENT_EXPR)
    {
      FilterXObject *e = filterx_expr_eval(elem->expr);
      if (!e)
        return FALSE;

      gboolean result = filterx_object_set_subscript(dict, e, &value);
      filterx_object_unref(e);
      return result;
    }

  g_assert_not_reached();
}


FilterXObject *
_dpath_touch(FilterXDPathLValue *self, gboolean append_mode)
{
  FilterXObject *dict = filterx_expr_eval(self->variable);
  if (!dict)
    return NULL;

  if (!filterx_object_is_type_or_ref(dict, &FILTERX_TYPE_NAME(dict)))
    {
      filterx_eval_push_error("dpath argument has non-dict element in path", &self->super, NULL);
      filterx_object_unref(dict);
      return NULL;
    }

  for (GList *elem = self->dpath_elements; elem; elem = elem->next)
    {
      if (!append_mode && !elem->next)
        break;

      gboolean error = FALSE;
      FilterXDPathElement *dpath_elem = (FilterXDPathElement *) elem->data;
      FilterXObject *value = filterx_dpath_elem_get(dict, dpath_elem, &error);

      if (error)
        {
          filterx_object_unref(dict);
          return NULL;
        }

      if (!value)
        {
          value = filterx_dict_new();
          filterx_object_cow_prepare(&value);

          filterx_dpath_elem_set(dict, dpath_elem, value);
        }

      filterx_object_unref(dict);
      dict = value;

      if (!filterx_object_is_type_or_ref(dict, &FILTERX_TYPE_NAME(dict)))
        {
          filterx_eval_push_error("dpath argument has non-dict element in path", &self->super, NULL);
          filterx_object_unref(dict);
          return NULL;
        }
    }

  return dict;
}

/* This call would let a dict reference to escape without copy-on-write protection.
 * FilterXDPathLValue should only appear on the left side of an assignment, and nowhere else.
 */
static FilterXObject *
_prohibit_eval(FilterXExpr *s)
{
  g_assert_not_reached();
}

static gboolean
filterx_dpath_lvalue_assign(FilterXExpr *s, FilterXObject **new_value)
{
  FilterXDPathLValue *self = (FilterXDPathLValue *) s;

  FilterXObject *last_element = _dpath_touch(self, self->append_mode);

  if (!last_element)
    return FALSE;

  gboolean result;
  if (self->append_mode)
    {
      if (!filterx_object_is_type_or_ref(*new_value, &FILTERX_TYPE_NAME(dict)))
        {
          filterx_eval_push_error("Right hand side of dpath()+= must be a dict", s, NULL);
          filterx_object_unref(last_element);
          return FALSE;
        }

      result = filterx_dict_merge(last_element, *new_value);
    }
  else
    result = filterx_dpath_elem_set(last_element, g_list_last(self->dpath_elements)->data, *new_value);

  filterx_object_unref(last_element);
  return result;
}

static FilterXExpr *
filterx_dpath_lvalue_optimize(FilterXExpr *s)
{
  FilterXDPathLValue *self = (FilterXDPathLValue *) s;

  self->variable = filterx_expr_optimize(self->variable);
  for (GList *elem = self->dpath_elements; elem; elem = elem->next)
    {
      FilterXDPathElement *dpath_elem = (FilterXDPathElement *) elem->data;
      if (dpath_elem->type == FILTERX_DPATH_ELEMENT_EXPR)
        {
          dpath_elem->expr = filterx_expr_optimize(dpath_elem->expr);

          if (filterx_expr_is_literal(dpath_elem->expr))
            {
              FilterXObject *literal = filterx_expr_eval(dpath_elem->expr);
              if (!literal)
                return NULL;

              elem->data = filterx_dpath_elem_object_new(literal);;
              filterx_dpath_elem_free(dpath_elem);
            }
        }
    }
  return NULL;
}

gboolean
filterx_dpath_lvalue_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXDPathLValue *self = (FilterXDPathLValue *) s;

  if (!filterx_expr_init(self->variable, cfg))
    return FALSE;

  for (GList *elem = self->dpath_elements; elem; elem = elem->next)
    {
      FilterXDPathElement *dpath_elem = (FilterXDPathElement *) elem->data;
      if (dpath_elem->type == FILTERX_DPATH_ELEMENT_EXPR)
        {
          if (!filterx_expr_init(dpath_elem->expr, cfg))
            return FALSE;
        }
    }

  return filterx_expr_init_method(s, cfg);
}

void
filterx_dpath_lvalue_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXDPathLValue *self = (FilterXDPathLValue *) s;

  filterx_expr_deinit(self->variable, cfg);

  for (GList *elem = self->dpath_elements; elem; elem = elem->next)
    {
      FilterXDPathElement *dpath_elem = (FilterXDPathElement *) elem->data;
      if (dpath_elem->type == FILTERX_DPATH_ELEMENT_EXPR)
        filterx_expr_deinit(dpath_elem->expr, cfg);
    }

  filterx_expr_deinit_method(s, cfg);
}

void
filterx_dpath_lvalue_free(FilterXExpr *s)
{
  FilterXDPathLValue *self = (FilterXDPathLValue *) s;

  filterx_expr_unref(self->variable);
  g_list_free_full(self->dpath_elements, (GDestroyNotify) filterx_dpath_elem_free);

  filterx_expr_free_method(s);
}

void
filterx_dpath_lvalue_set_append_mode(FilterXExpr *s, gboolean append_mode)
{
  FilterXDPathLValue *self = (FilterXDPathLValue *) s;
  self->append_mode = append_mode;
}

FilterXExpr *
filterx_dpath_lvalue_new(FilterXExpr *variable, GList *dpath_elements, GError **error)
{
  if (!dpath_elements)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "There must be at least one element in the path");
      return NULL;
    }

  FilterXDPathLValue *self = g_new0(FilterXDPathLValue, 1);
  filterx_expr_init_instance(&self->super, "dpath_lvalue");

  self->super.eval = _prohibit_eval;
  self->super.assign = filterx_dpath_lvalue_assign;
  self->super.optimize = filterx_dpath_lvalue_optimize;
  self->super.init = filterx_dpath_lvalue_init;
  self->super.deinit = filterx_dpath_lvalue_deinit;
  self->super.free_fn = filterx_dpath_lvalue_free;

  self->variable = variable;
  self->dpath_elements = dpath_elements;

  return &self->super;
}

/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/expr-literal-container.h"
#include "filterx/expr-literal.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object.h"
#include "filterx/object-null.h"
#include "filterx/object-message-value.h"
#include "filterx/object-extractor.h"

/* Object Members (e.g. key-value) */

struct FilterXLiteralElement_
{
  FilterXExpr *key;
  FilterXExpr *value;
  guint8 nullv:1,
         literal:1;
};

static gboolean
_literal_element_init(FilterXLiteralElement *self, GlobalConfig *cfg)
{
  if (!filterx_expr_init(self->key, cfg))
    return FALSE;

  if (!filterx_expr_init(self->value, cfg))
    {
      filterx_expr_deinit(self->key, cfg);
      return FALSE;
    }

  return TRUE;
}

static void
_literal_element_optimize(FilterXLiteralElement *self)
{
  self->key = filterx_expr_optimize(self->key);
  self->value = filterx_expr_optimize(self->value);
  self->literal = (!self->key || filterx_expr_is_literal(self->key)) && filterx_expr_is_literal(self->value);
}

static void
_literal_element_deinit(FilterXLiteralElement *self, GlobalConfig *cfg)
{
  filterx_expr_deinit(self->key, cfg);
  filterx_expr_deinit(self->value, cfg);
}

static void
_literal_element_free(FilterXLiteralElement *self)
{
  filterx_expr_unref(self->key);
  filterx_expr_unref(self->value);
  g_free(self);
}

FilterXLiteralElement *
filterx_literal_element_new(FilterXExpr *key, FilterXExpr *value)
{
  FilterXLiteralElement *self = g_new0(FilterXLiteralElement, 1);

  self->key = key;
  self->value = value;

  return self;
}

FilterXLiteralElement *
filterx_nullv_literal_element_new(FilterXExpr *key, FilterXExpr *value)
{
  FilterXLiteralElement *self = filterx_literal_element_new(key, value);

  self->nullv = TRUE;

  return self;
}


/* Literal Object */

struct FilterXLiteralContainer_
{
  FilterXExpr super;
  FilterXObject *(*create_container)(FilterXLiteralContainer *self);
  GList *elements;
};

gsize
filterx_literal_container_len(FilterXExpr *s)
{
  GList *elements = NULL;

  elements = ((FilterXLiteralContainer *) s)->elements;
  return g_list_length(elements);
}

static inline FilterXObject *
_literal_container_eval_expr(FilterXExpr *expr, gboolean early_eval)
{
  if (early_eval)
    return filterx_literal_get_value(expr);
  else
    return filterx_expr_eval(expr);
}

static inline FilterXObject *
_literal_container_eval(FilterXExpr *s, gboolean early_eval)
{
  FilterXLiteralContainer *self = (FilterXLiteralContainer *) s;

  FilterXObject *result = self->create_container(self);
  filterx_object_cow_prepare(&result);
  for (GList *link = self->elements; link; link = link->next)
    {
      FilterXLiteralElement *elem = (FilterXLiteralElement *) link->data;

      FilterXObject *key = NULL;
      if (elem->key)
        {
          key = _literal_container_eval_expr(elem->key, early_eval);
          if (!key)
            {
              filterx_eval_push_error_static_info("Failed create literal container", s, "Failed to evaluate key");
              goto error;
            }
        }

      FilterXObject *value = _literal_container_eval_expr(elem->value, early_eval);
      if (elem->nullv)
        {
          if (!value)
            filterx_eval_dump_errors("FilterX: null coalesce assignment suppressing error");

          if (!value || filterx_object_extract_null(value))
            {
              filterx_object_unref(key);
              filterx_object_unref(value);
              continue;
            }
        }

      if (!value)
        {
          filterx_eval_push_error_static_info("Failed create literal container", s, "Failed to evaluate value");
          filterx_object_unref(key);
          goto error;
        }

      value = filterx_object_cow_fork2(value, NULL);
      gboolean success = filterx_object_set_subscript(result, key, &value);

      filterx_object_unref(key);
      filterx_object_unref(value);

      if (!success)
        {
          filterx_eval_push_error_static_info("Failed create literal container", s, "Failed to set value in container");
          goto error;
        }
    }

  return result;
error:
  filterx_object_unref(result);
  return NULL;
}

/* evaluate during optimize while the expressions are yet to be initialized */
static FilterXObject *
_literal_container_eval_early(FilterXExpr *s)
{
  return _literal_container_eval(s, TRUE);
}

static FilterXObject *
_literal_container_eval_runtime(FilterXExpr *s)
{
  return _literal_container_eval(s, FALSE);
}

static FilterXExpr *
_literal_container_optimize(FilterXExpr *s)
{
  FilterXLiteralContainer *self = (FilterXLiteralContainer *) s;

  gboolean literal = TRUE;
  for (GList *link = self->elements; link; link = link->next)
    {
      FilterXLiteralElement *elem = (FilterXLiteralElement *) link->data;

      _literal_element_optimize(elem);
      if (!elem->literal)
        literal = FALSE;
    }
  if (literal)
    return filterx_literal_new(_literal_container_eval_early(s));

  return NULL;
}

static gboolean
_literal_container_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXLiteralContainer *self = (FilterXLiteralContainer *) s;

  for (GList *link = self->elements; link; link = link->next)
    {
      FilterXLiteralElement *elem = (FilterXLiteralElement *) link->data;

      if (!_literal_element_init(elem, cfg))
        {
          for (GList *deinit_link = self->elements; deinit_link != link; deinit_link = deinit_link->next)
            {
              elem = (FilterXLiteralElement *) deinit_link->data;
              _literal_element_deinit(elem, cfg);
            }
          return FALSE;
        }
    }

  return filterx_expr_init_method(s, cfg);
}

static void
_literal_container_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXLiteralContainer *self = (FilterXLiteralContainer *) s;

  for (GList *link = self->elements; link; link = link->next)
    {
      FilterXLiteralElement *elem = (FilterXLiteralElement *) link->data;
      _literal_element_deinit(elem, cfg);
    }

  filterx_expr_deinit_method(s, cfg);
}

void
_literal_container_free(FilterXExpr *s)
{
  FilterXLiteralContainer *self = (FilterXLiteralContainer *) s;

  g_list_free_full(self->elements, (GDestroyNotify) _literal_element_free);
  filterx_expr_free_method(s);
}

static void
_literal_container_init_instance(FilterXLiteralContainer *self, const gchar *type)
{
  filterx_expr_init_instance(&self->super, type, FALSE);
  self->super.eval = _literal_container_eval_runtime;
  self->super.optimize = _literal_container_optimize;
  self->super.init = _literal_container_init;
  self->super.deinit = _literal_container_deinit;
  self->super.free_fn = _literal_container_free;
}

/* Literal dict objects */

gboolean
filterx_literal_dict_foreach(FilterXExpr *s, FilterXLiteralDictForeachFunc func, gpointer user_data)
{
  GList *elements = ((FilterXLiteralContainer *) s)->elements;

  for (GList *link = elements; link; link = link->next)
    {
      FilterXLiteralElement *elem = (FilterXLiteralElement *) link->data;

      if (!func(elem->key, elem->value, user_data))
        return FALSE;
    }

  return TRUE;
}

static FilterXObject *
_literal_dict_create(FilterXLiteralContainer *s)
{
  return filterx_dict_new();
}

FilterXExpr *
filterx_literal_dict_new(GList *elements)
{
  FilterXLiteralContainer *self = g_new0(FilterXLiteralContainer, 1);

  _literal_container_init_instance(self, FILTERX_EXPR_TYPE_NAME(literal_dict));
  self->create_container = _literal_dict_create;
  self->elements = elements;

  return &self->super;
}

/* Literal list objects */

gboolean
filterx_literal_list_foreach(FilterXExpr *s, FilterXLiteralListForeachFunc func, gpointer user_data)
{
  GList *elements = ((FilterXLiteralContainer *) s)->elements;

  gsize i = 0;
  for (GList *link = elements; link; link = link->next)
    {
      FilterXLiteralElement *elem = (FilterXLiteralElement *) link->data;

      if (!func(i, elem->value, user_data))
        return FALSE;

      i++;
    }

  return TRUE;
}

static FilterXObject *
_literal_list_create(FilterXLiteralContainer *s)
{
  return filterx_list_new();
}

FilterXExpr *
filterx_literal_list_new(GList *elements)
{
  FilterXLiteralContainer *self = g_new0(FilterXLiteralContainer, 1);

  _literal_container_init_instance(self, FILTERX_EXPR_TYPE_NAME(literal_list));
  self->create_container = _literal_list_create;
  self->elements = elements;

  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(literal_list);
FILTERX_EXPR_DEFINE_TYPE(literal_dict);

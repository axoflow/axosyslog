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

#include <criterion/criterion.h>
#include <stdarg.h>
#include "filterx-lib.h"
#include "cr_template.h"
#include "filterx/json-repr.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-string.h"
#include "filterx/expr-compound.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/filterx-eval.h"

void
assert_marshaled_object(FilterXObject *obj, const gchar *repr, LogMessageValueType type)
{
  GString *b = g_string_sized_new(0);
  LogMessageValueType t;

  /* check if we _overwrite_ the string with the marshalled value */
  g_string_append(b, "PREFIX");

  cr_assert(filterx_object_marshal(obj, b, &t) == TRUE);
  cr_assert_str_eq(b->str, repr);
  cr_assert_eq(t, type);
  g_string_free(b, TRUE);
}

void
assert_object_json_equals(FilterXObject *obj, const gchar *expected_json_repr)
{
  GString *b = g_string_sized_new(0);

  filterx_object_to_json(obj, b);
  cr_assert_str_eq(b->str, expected_json_repr);
  g_string_free(b, TRUE);
}

void
assert_object_repr_equals(FilterXObject *obj, const gchar *expected_repr)
{
  GString *repr = g_string_new("foobar");
  gsize len = repr->len;

  cr_assert(filterx_object_repr_append(obj, repr) == TRUE);
  cr_assert_str_eq(repr->str + len, expected_repr);
  g_string_free(repr, TRUE);
}

void
assert_object_str_equals(FilterXObject *obj, const gchar *expected_str)
{
  GString *str = g_string_new("foobar");
  gsize len = str->len;

  cr_assert(filterx_object_str_append(obj, str) == TRUE);
  cr_assert_str_eq(str->str + len, expected_str);
  g_string_free(str, TRUE);
}

FilterXObject *
filterx_test_dict_new(void)
{
  FilterXObject *result = filterx_dict_new();
  result->type = &FILTERX_TYPE_NAME(test_dict);
  return result;
}

FilterXObject *
filterx_test_list_new(void)
{
  FilterXObject *result = filterx_list_new();
  result->type = &FILTERX_TYPE_NAME(test_list);
  return result;
}

const gchar *
filterx_test_unknown_object_marshaled_repr(gssize *len)
{
  static const gchar *marshaled = "test_unknown_object_marshaled";
  if (len)
    *len = strlen(marshaled);
  return marshaled;
}

const gchar *
filterx_test_unknown_object_repr(gssize *len)
{
  static const gchar *repr = "test_unknown_object_repr";
  if (len)
    *len = strlen(repr);
  return repr;
}

static gboolean
_unknown_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  *t = LM_VT_STRING;
  g_string_append(repr, filterx_test_unknown_object_marshaled_repr(NULL));
  return TRUE;
}

static gboolean
_unknown_repr(FilterXObject *s, GString *repr)
{
  g_string_append(repr, filterx_test_unknown_object_repr(NULL));
  return TRUE;
}

static gboolean
_unknown_format_json(FilterXObject *s, GString *json)
{
  gssize len;
  const gchar *str = filterx_test_unknown_object_marshaled_repr(&len);
  return string_format_json(str, len, TRUE, json);
}

static gboolean
_unknown_truthy(FilterXObject *s)
{
  return TRUE;
}

FilterXObject *
filterx_test_unknown_object_new(void)
{
  return filterx_object_new(&FILTERX_TYPE_NAME(test_unknown_object));
}

typedef struct _FilterXExprWrapperExpr
{
  FilterXExpr super;
  FilterXExpr *block;
} FilterXExprWrapperExpr;

static FilterXObject *
_expr_wrapper_eval(FilterXExpr *s)
{
  FilterXExprWrapperExpr *self = (FilterXExprWrapperExpr *) s;
  FilterXObject *result = filterx_expr_eval(self->block);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to evaluate non-literal", "Failed to evaluate expression");
      return NULL;
    }
  return result;
}

static void
_expr_wrapper_free(FilterXExpr *s)
{
  FilterXExprWrapperExpr *self = (FilterXExprWrapperExpr *) s;
  filterx_expr_unref(self->block);
  filterx_expr_free_method(s);
}

static gboolean
_expr_wrapper_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXExprWrapperExpr *self = (FilterXExprWrapperExpr *) s;

  return filterx_expr_visit(s, &self->block, f, user_data);
}

FilterXExpr *
filterx_expr_wrapper_new(FilterXExpr *expr)
{
  FilterXExprWrapperExpr *self = g_new0(FilterXExprWrapperExpr, 1);

  filterx_expr_init_instance(&self->super, "expr-wrapper", FXE_READ);
  self->super.eval = _expr_wrapper_eval;
  self->super.walk_children = _expr_wrapper_walk;
  self->super.free_fn = _expr_wrapper_free;

  self->block = expr;

  return &self->super;
}

typedef struct _FilterXObjectExpr
{
  FilterXExpr super;
  FilterXObject *object;
} FilterXObjectExpr;

static FilterXObject *
_object_expr_eval(FilterXExpr *s)
{
  FilterXObjectExpr *self = (FilterXObjectExpr *) s;
  return filterx_object_ref(self->object);
}

static gboolean
_object_expr_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  return TRUE;
}

static void
_object_expr_free(FilterXExpr *s)
{
  FilterXObjectExpr *self = (FilterXObjectExpr *) s;
  filterx_object_unref(self->object);
  filterx_expr_free_method(s);
}

FilterXExpr *
filterx_object_expr_new(FilterXObject *object)
{
  FilterXObjectExpr *self = g_new0(FilterXObjectExpr, 1);

  filterx_expr_init_instance(&self->super, "object-expr", FXE_READ);
  self->super.eval = _object_expr_eval;
  self->super.walk_children = _object_expr_walk;
  self->super.free_fn = _object_expr_free;
  self->object = object;
  return &self->super;
}

typedef struct _FilterXDummyError FilterXDummyError;

struct _FilterXDummyError
{
  FilterXExpr super;
  gchar *msg;
};

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXDummyError *self = (FilterXDummyError *)s;
  filterx_eval_push_error(self->msg, NULL);
  return NULL;
}

static void
_free(FilterXExpr *s)
{
  FilterXDummyError *self = (FilterXDummyError *)s;
  g_free(self->msg);
  filterx_expr_free_method(s);
}

static gboolean
_dummy_error_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  return TRUE;
}

FilterXExpr *
filterx_dummy_error_new(const gchar *msg)
{
  FilterXDummyError *self = g_new0(FilterXDummyError, 1);
  self->msg = g_strdup(msg);
  filterx_expr_init_instance(&self->super, "dummy", FXE_READ);
  self->super.eval = _eval;
  self->super.walk_children = _dummy_error_walk;
  self->super.free_fn = _free;
  return &self->super;
}

void
filterx_test_expr_set_location_with_text(FilterXExpr *expr, const gchar *filename,
                                         gint first_line, gint first_column, gint last_line, gint last_column,
                                         const gchar *text)
{
  CFG_LTYPE lloc =
  {
    .name = filename,
    .first_line = first_line,
    .first_column = first_column,
    .last_line = last_line,
    .last_column = last_column
  };
  filterx_expr_set_location_with_text(expr, &lloc, text);
}

FilterXObject *
init_and_eval_expr(FilterXExpr *expr)
{
  cr_assert(filterx_expr_init(expr, configuration));
  FilterXObject *result = filterx_expr_eval(expr);
  filterx_expr_deinit(expr, configuration);
  return result;
}

static struct
{
  LogMessage *msg;
  FilterXScope *scope;
  FilterXEnvironment test_env;
  FilterXEvalContext context;
} filterx_world;

void
init_libtest_filterx(void)
{
  filterx_type_init(&FILTERX_TYPE_NAME(test_unknown_object));

  /* These will use the json implementations, but won't be json typed. */
  filterx_type_init(&FILTERX_TYPE_NAME(test_dict));
  filterx_type_init(&FILTERX_TYPE_NAME(test_list));
  FILTERX_TYPE_NAME(test_dict) = FILTERX_TYPE_NAME(dict);
  FILTERX_TYPE_NAME(test_list) = FILTERX_TYPE_NAME(list);

  /* We are setting up a context that allows us to exercise most things.
   *
   *   1) compile-time things (freeze, dedup): allocator is unset, e.g.  all
   *      objects will be heap allocated.  FilterXEnvironment is populated,
   *      e.g.  dedup storage, weak refs, etc are available.
   *
   *   2) production-time things (eval, error state): msg and scope are both
   *      set.
   *
   * Objects are never allocated from the thread-specific allocator,
   * everything is on the heap.
   */
  filterx_world.msg = create_sample_message();
  filterx_world.scope = filterx_scope_new(NULL, NULL);
  filterx_env_init(&filterx_world.test_env);
  filterx_eval_begin_context(&filterx_world.context, NULL, filterx_world.scope, filterx_world.msg);
  filterx_world.context.env = &filterx_world.test_env;
  filterx_world.context.allocator = NULL;
}

void
set_libtest_filterx_scope(FilterXScope *scope)
{
  FilterXScope *old_scope = filterx_world.scope;

  filterx_scope_set_message(scope, filterx_world.msg);
  filterx_world.context.scope = scope;
  filterx_world.scope = scope;

  if (old_scope)
    filterx_scope_free(old_scope);
}

void
deinit_libtest_filterx(void)
{
  log_msg_unref(filterx_world.msg);
  filterx_scope_free(filterx_world.scope);
  filterx_eval_clear_errors();
  filterx_eval_end_context(&filterx_world.context);
  filterx_env_clear(&filterx_world.test_env);
}

static FilterXExpr *
_literal_container_of_va(FilterXExpr *(*ctor)(GList *elements), FilterXExpr *first, va_list args)
{
  GList *elements = NULL;
  for (FilterXExpr *elem = first; elem != NULL; elem = va_arg(args, FilterXExpr *))
    elements = g_list_append(elements, filterx_literal_element_new(NULL, elem));
  return ctor(elements);
}

FilterXExpr *
filterx_literal_tuple_of(FilterXExpr *first, ...)
{
  va_list args;
  va_start(args, first);
  FilterXExpr *result = _literal_container_of_va(filterx_literal_tuple_new, first, args);
  va_end(args);
  return result;
}

FilterXExpr *
filterx_literal_list_of(FilterXExpr *first, ...)
{
  va_list args;
  va_start(args, first);
  FilterXExpr *result = _literal_container_of_va(filterx_literal_list_new, first, args);
  va_end(args);
  return result;
}

FilterXExpr *
filterx_literal_list_of_objects(FilterXObject *first, ...)
{
  GList *elements = NULL;
  va_list args;
  va_start(args, first);
  for (FilterXObject *obj = first; obj != NULL; obj = va_arg(args, FilterXObject *))
    elements = g_list_append(elements, filterx_literal_element_new(NULL, filterx_literal_new(obj)));
  va_end(args);
  return filterx_literal_list_new(elements);
}

FilterXExpr *
filterx_literal_dict_of(FilterXExpr *first_key, FilterXExpr *first_value, ...)
{
  GList *elements = NULL;
  va_list args;
  va_start(args, first_value);
  FilterXExpr *key = first_key;
  FilterXExpr *value = first_value;
  while (key != NULL)
    {
      elements = g_list_append(elements, filterx_literal_element_new(key, value));
      key = va_arg(args, FilterXExpr *);
      if (key != NULL)
        value = va_arg(args, FilterXExpr *);
    }
  va_end(args);
  return filterx_literal_dict_new(elements);
}

FILTERX_DEFINE_TYPE(test_dict, FILTERX_TYPE_NAME(object), .is_abstract = TRUE);
FILTERX_DEFINE_TYPE(test_list, FILTERX_TYPE_NAME(object), .is_abstract = TRUE);
FILTERX_DEFINE_TYPE(test_unknown_object, FILTERX_TYPE_NAME(object),
                    .is_mutable = FALSE,
                    .is_abstract = TRUE,
                    .truthy = _unknown_truthy,
                    .marshal = _unknown_marshal,
                    .repr = _unknown_repr,
                    .format_json = _unknown_format_json);

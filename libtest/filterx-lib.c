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
#include "filterx-lib.h"
#include "cr_template.h"
#include "filterx/json-repr.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-string.h"
#include "filterx/expr-compound.h"
#include "filterx/expr-literal.h"
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
  return string_format_json(str, len, json);
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

typedef struct _FilterXNonLiteralExpr
{
  FilterXExpr super;
  FilterXExpr *block;
} FilterXNonLiteralExpr;

static FilterXObject *
_non_literal_eval(FilterXExpr *s)
{
  FilterXNonLiteralExpr *self = (FilterXNonLiteralExpr *) s;
  FilterXObject *result = filterx_expr_eval(self->block);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to evaluate non-literal", s, "Failed to evaluate expression");
      return NULL;
    }
  return result;
}

static FilterXExpr *
_non_literal_optimize(FilterXExpr *s)
{
  FilterXNonLiteralExpr *self = (FilterXNonLiteralExpr *) s;
  self->block = filterx_expr_optimize(self->block);
  return NULL;
}

static gboolean
_non_literal_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXNonLiteralExpr *self = (FilterXNonLiteralExpr *) s;
  if (!filterx_expr_init(self->block, cfg))
    return FALSE;
  return filterx_expr_init_method(s, cfg);
}

static void
_non_literal_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXNonLiteralExpr *self = (FilterXNonLiteralExpr *) s;
  filterx_expr_deinit(self->block, cfg);
  filterx_expr_deinit_method(s, cfg);
}

static void
_non_literal_free(FilterXExpr *s)
{
  FilterXNonLiteralExpr *self = (FilterXNonLiteralExpr *) s;
  filterx_expr_unref(self->block);
  filterx_expr_free_method(s);
}

FilterXExpr *
filterx_non_literal_new_from_expr(FilterXExpr *expr)
{
  FilterXNonLiteralExpr *self = g_new0(FilterXNonLiteralExpr, 1);

  filterx_expr_init_instance(&self->super, "non-literal");
  self->super.eval = _non_literal_eval;
  self->super.init = _non_literal_init;
  self->super.deinit = _non_literal_deinit;
  self->super.optimize = _non_literal_optimize;
  self->super.free_fn = _non_literal_free;

  self->block = filterx_compound_expr_new(TRUE);
  filterx_compound_expr_add_ref(self->block, expr);

  return &self->super;
}

FilterXExpr *
filterx_non_literal_new(FilterXObject *object)
{
  return filterx_non_literal_new_from_expr(filterx_literal_new(object));
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
  filterx_eval_push_error(self->msg, s, NULL);
  return NULL;
}

static void
_free(FilterXExpr *s)
{
  FilterXDummyError *self = (FilterXDummyError *)s;
  g_free(self->msg);
  filterx_expr_free_method(s);
}

FilterXExpr *
filterx_dummy_error_new(const gchar *msg)
{
  FilterXDummyError *self = g_new0(FilterXDummyError, 1);
  self->msg = g_strdup(msg);
  filterx_expr_init_instance(&self->super, "dummy");
  self->super.eval = _eval;
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
  FilterXEvalContext context;
} filterx_env;

void
init_libtest_filterx(void)
{
  filterx_type_init(&FILTERX_TYPE_NAME(test_unknown_object));

  /* These will use the json implementations, but won't be json typed. */
  filterx_type_init(&FILTERX_TYPE_NAME(test_dict));
  filterx_type_init(&FILTERX_TYPE_NAME(test_list));
  FILTERX_TYPE_NAME(test_dict) = FILTERX_TYPE_NAME(dict);
  FILTERX_TYPE_NAME(test_list) = FILTERX_TYPE_NAME(list);

  filterx_env.msg = create_sample_message();
  filterx_env.scope = filterx_scope_new(NULL);
  filterx_eval_begin_context(&filterx_env.context, NULL, filterx_env.scope, filterx_env.msg);
}

void
deinit_libtest_filterx(void)
{
  log_msg_unref(filterx_env.msg);
  filterx_scope_free(filterx_env.scope);
  filterx_eval_end_context(&filterx_env.context);
}

FILTERX_DEFINE_TYPE(test_dict, FILTERX_TYPE_NAME(object));
FILTERX_DEFINE_TYPE(test_list, FILTERX_TYPE_NAME(object));
FILTERX_DEFINE_TYPE(test_unknown_object, FILTERX_TYPE_NAME(object),
                    .is_mutable = FALSE,
                    .truthy = _unknown_truthy,
                    .marshal = _unknown_marshal,
                    .repr = _unknown_repr,
                    .format_json = _unknown_format_json);

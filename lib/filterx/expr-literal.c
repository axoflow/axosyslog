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
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"

typedef struct _FilterXLiteral
{
  FilterXExpr super;
  FilterXObject *object;
} FilterXLiteral;

FilterXObject *
filterx_literal_get_value(FilterXExpr *s)
{
  g_assert(filterx_expr_is_literal(s));
  FilterXLiteral *self = (FilterXLiteral *) s;

  return filterx_object_ref(self->object);
}

const gchar *
filterx_literal_key_expr_to_path_step(FilterXExpr *key_expr)
{
  /* the grammar really does hand us a NULL key expression, for the `expr[] = v` append form */
  if (!key_expr || !filterx_expr_is_literal(key_expr))
    return NULL;

  FilterXObject *literal = filterx_literal_get_value(key_expr);
  if (!literal)
    return NULL;

  const gchar *step = NULL;
  if (filterx_object_is_type_or_ref(literal, &FILTERX_TYPE_NAME(string)))
    step = filterx_access_path_intern_key(filterx_string_get_value_ref_and_assert_nul(literal, NULL));

  filterx_object_unref(literal);
  return step;
}

static FilterXObject *
_eval_literal(FilterXExpr *s)
{
  FilterXLiteral *self = (FilterXLiteral *) s;
  return filterx_object_ref(self->object);
}

static void
_free(FilterXExpr *s)
{
  FilterXLiteral *self = (FilterXLiteral *) s;
  filterx_object_unref(self->object);
  filterx_expr_free_method(s);
}

static gboolean
_literal_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  return TRUE;
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

/* Only this node's own kind; whatever nesting the object has is installed into the env by the
 * write that stores it somewhere, which is the only place a path exists to install it at. */
static FilterXStaticType
_kind_from_filterx_object(FilterXObject *obj)
{
  if (!obj)
    return FILTERX_STATIC_TYPE_UNKNOWN;

  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(string)))
    return FILTERX_STATIC_TYPE_STRING;

  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(integer)))
    return FILTERX_STATIC_TYPE_INTEGER;

  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(double)))
    return FILTERX_STATIC_TYPE_DOUBLE;

  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(dict)))
    return FILTERX_STATIC_TYPE_DICT;

  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(list)))
    return FILTERX_STATIC_TYPE_LIST;

  return FILTERX_STATIC_TYPE_UNKNOWN;
}

static void
_literal_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  FilterXLiteral *self = (FilterXLiteral *) s;
  s->static_type = _kind_from_filterx_object(self->object);
}

static FilterXIRValue
_literal_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXLiteral *self = (FilterXLiteral *) s;

  return fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->object));
}

#endif

/* NOTE: takes the object reference */
void
filterx_literal_init_instance(FilterXLiteral *s, FilterXObject *object)
{
  FilterXLiteral *self = (FilterXLiteral *) s;

  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(literal), FXE_READ);
  self->super.eval = _eval_literal;
  self->super.walk_children = _literal_walk;
  self->super.free_fn = _free;
#if SYSLOG_NG_ENABLE_JIT
  self->super.infer_types = _literal_infer_types;
  self->super.compile = _literal_compile;
#endif
  self->object = object;
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_literal_new(FilterXObject *object)
{
  FilterXLiteral *self = g_new0(FilterXLiteral, 1);

  filterx_literal_init_instance(self, object);
  filterx_eval_freeze_object(&self->object);
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(literal);

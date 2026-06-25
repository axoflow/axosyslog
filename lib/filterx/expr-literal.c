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

/* Recursively determine the full FilterXStaticTypeSpec for a literal FilterXObject.
 * Walks dict / list elements to compute element types, bottoming out at STRING / leaf
 * types. The recursion follows the literal's own (finite) nesting — there is no depth cap. */
typedef struct
{
  FilterXStaticTypeSpec spec;   /* the meet of all elements seen so far (valid once seen) */
  gboolean seen;                /* whether any element has been observed yet */
} _LiteralElementMeetCtx;

static FilterXStaticTypeSpec _spec_from_filterx_object(FilterXObject *obj);

static gboolean
_collect_value_spec(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  _LiteralElementMeetCtx *ctx = (_LiteralElementMeetCtx *) user_data;
  FilterXStaticTypeSpec value_spec = _spec_from_filterx_object(value);
  /* The `seen` flag distinguishes "no elements yet" from "elements meet to UNKNOWN":
   * the first element seeds the accumulator, subsequent ones actually meet. */
  if (!ctx->seen)
    {
      ctx->spec = value_spec;
      ctx->seen = TRUE;
    }
  else
    ctx->spec = filterx_static_type_spec_meet(ctx->spec, value_spec);
  /* Stop iterating once we've degraded to UNKNOWN — further meets can only stay there. */
  return ctx->spec != FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
}

static FilterXStaticTypeSpec
_spec_from_filterx_object(FilterXObject *obj)
{
  if (!obj)
    return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;

  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(string)))
    return filterx_static_type_kind_only(FILTERX_STATIC_TYPE_STRING);

  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(integer)))
    return filterx_static_type_kind_only(FILTERX_STATIC_TYPE_INTEGER);

  FilterXStaticType outer_kind = FILTERX_STATIC_TYPE_UNKNOWN;
  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(dict)))
    outer_kind = FILTERX_STATIC_TYPE_DICT;
  else if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(list)))
    outer_kind = FILTERX_STATIC_TYPE_LIST;
  else
    return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;

  /* Determine the meet of all element specs. The `seen` flag distinguishes "no elements
   * observed yet" from "element type is UNKNOWN". An empty container ends the iter unseen
   * and gets a FRESH element type — "element not yet committed" — so that the first write to
   * it lifts the element to the written value's type (see
   * filterx_type_env_update_on_write). FRESH is stripped before it reaches codegen. */
  _LiteralElementMeetCtx ctx = { .spec = FILTERX_STATIC_TYPE_UNKNOWN_SPEC, .seen = FALSE };
  filterx_object_iter(obj, _collect_value_spec, &ctx);
  FilterXStaticTypeSpec element_spec = ctx.seen
                                       ? ctx.spec
                                       : filterx_static_type_kind_only(FILTERX_STATIC_TYPE_FRESH);
  return filterx_static_type_make_container(outer_kind, element_spec);
}

static void
_literal_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  FilterXLiteral *self = (FilterXLiteral *) s;
  s->static_type = _spec_from_filterx_object(self->object);
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

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
  self->super.infer_types = _literal_infer_types;
#if SYSLOG_NG_ENABLE_JIT
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

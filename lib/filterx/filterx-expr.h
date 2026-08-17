/*
 * Copyright (c) 2024 Axoflow
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

#ifndef FILTERX_EXPR_H_INCLUDED
#define FILTERX_EXPR_H_INCLUDED

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"
#include "filterx-object.h"
#include "filterx/filterx-variable.h"
#include "filterx/filterx-type-inference.h"
#include "cfg-lexer.h"
#include "stats/stats-counter.h"

#include <string.h>

/* A location inside a variable: the root variable's handle plus the key steps that lead to it.
 * The zero-step path addresses the variable itself.  Every step is a key this pass can name, so a
 * path either addresses one location exactly or does not address it at all.
 *
 * Steps are interned into a permanent pool, so a path outlives the expression it was peeled
 * from -- FilterXExpr::name borrows its characters from an object the expression owns and frees.
 * Interning is also what makes comparing two steps a pointer comparison.
 */

#define FILTERX_PATH_MAX_DEPTH 8

typedef struct _FilterXTypePath
{
  FilterXVariableHandle root;
  guint n_steps;
  /* TRUE when the location has no exact address and steps[] holds the representable prefix of it.
   * Two things do that: nesting deeper than FILTERX_PATH_MAX_DEPTH, and a step no key can name --
   * a computed subscript, a list index, the `expr[] = v` append.  Both leave the same obligation
   * on a write, which is why they are the same bit: nothing under the prefix survives. */
  gboolean truncated;
  const gchar *steps[FILTERX_PATH_MAX_DEPTH];
} FilterXTypePath;

const gchar *filterx_type_path_intern_key(const gchar *key);

/* Orders paths so that a path is immediately followed by exactly its own descendants, which makes
 * every subtree a contiguous range.  Roots compare numerically, then step by step, then the
 * shorter path first. */
gint filterx_type_path_compare(gconstpointer a, gconstpointer b, gpointer user_data);

static inline gboolean
filterx_type_path_is_prefix_of(const FilterXTypePath *prefix, const FilterXTypePath *path)
{
  if (prefix->root != path->root || prefix->n_steps > path->n_steps)
    return FALSE;
  for (guint i = 0; i < prefix->n_steps; i++)
    {
      if (prefix->steps[i] != path->steps[i])
        return FALSE;
    }
  return TRUE;
}

static inline void
filterx_type_path_parent(const FilterXTypePath *self, FilterXTypePath *parent_out)
{
  *parent_out = *self;
  if (parent_out->n_steps > 0)
    parent_out->n_steps--;
}

/* Appending to a path that already lost its exact address keeps it lost: `d[$k].a` must not read
 * as `d.a`, because the write may have landed under any key of d rather than under that one. */
static inline gboolean
filterx_type_path_append_step(FilterXTypePath *self, const gchar *step)
{
  if (self->truncated)
    return FALSE;

  if (!step || self->n_steps >= FILTERX_PATH_MAX_DEPTH)
    {
      self->truncated = TRUE;
      return FALSE;
    }
  self->steps[self->n_steps++] = step;
  return TRUE;
}

FilterXTypePath *filterx_type_path_dup(const FilterXTypePath *self);

/* The step a subscript key expression names: the interned literal string, or NULL when it names
 * no step at all.
 *
 * A getattr and a subscript with a literal string key produce the same step, so $a.b and $a["b"]
 * address the same location.  Every other key -- a computed one, every list index, the NULL key
 * expression the `expr[] = v` append form hands us -- names nothing, and appending it truncates
 * the path.
 *
 * Defined in expr-literal.c: it inspects a literal, and filterx-expr.c -- the base class -- has
 * no business including a concrete expression's header. */
const gchar *filterx_type_path_step_from_key_expr(FilterXExpr *key_expr);

#define FXE_EFFECT_BITFIELD_SIZE 3
typedef enum
{
  /* reads variables */
  FXE_READ=0,
  /* writes to a variable */
  FXE_WRITE=1 << 0,
  /* impacts the outside world, e.g. I/O, metrics, etc */
  FXE_WORLD=1 << 1,
  /* control flow change, e.g. break */
  FXE_CONTROL=1 << 2,
  FXE_MAX=FXE_CONTROL,
} FilterXEffect;

G_STATIC_ASSERT((1 << FXE_EFFECT_BITFIELD_SIZE) > FXE_MAX);

typedef gboolean (*FilterXExprWalkFunc)(FilterXExpr *parent, FilterXExpr **child, gpointer user_data);

struct _FilterXExpr
{
  StatsCounterItem *eval_count;
  /* evaluate expression */
  FilterXObject *(*eval)(FilterXExpr *self);

  /* not thread-safe */
  guint32 ref_cnt;
guint32 ignore_falsy_result:1, suppress_from_trace:1, inited:1, optimized:1, statement:1, types_inferred:1, effects:
  FXE_EFFECT_BITFIELD_SIZE;

  /* not to be used except for FilterXMessageRef, replace any cached values
   * with the unmarshaled version */
  void (*_update_repr)(FilterXExpr *self, FilterXObject **new_repr);

  /* assign a new value to this expr */
  gboolean (*assign)(FilterXExpr *self, FilterXObject **new_value);
  /* += a value to this expr */
  FilterXObject *(*plus_assign)(FilterXExpr *self, FilterXObject *new_value);

  /* is the expression set? */
  gboolean (*is_set)(FilterXExpr *self);
  /* unset the expression */
  gboolean (*unset)(FilterXExpr *self);
  /* move the expression */
  FilterXObject *(*move)(FilterXExpr *self);

  gboolean (*init)(FilterXExpr *self, GlobalConfig *cfg);
  void (*deinit)(FilterXExpr *self, GlobalConfig *cfg);
  FilterXExpr *(*optimize)(FilterXExpr *self);
#if SYSLOG_NG_ENABLE_JIT
  FilterXIRValue (*compile)(FilterXExpr *self, FilterXJIT *jit);
  FilterXIRValue (*compile_assign)(FilterXExpr *self, FilterXJIT *jit, FilterXIRValue new_value);
  void (*infer_types)(FilterXExpr *self, FilterXTypeEnv *env);
#endif

  /* Outside the guard on purpose: the hooks above need FilterXIRValue and
   * FilterXJIT, this does not.  Guarding it would break every unguarded
   * assignment to it, and the inference pass is a no-op without the JIT
   * anyway, so the field simply stays UNKNOWN there. */
  FilterXStaticType static_type;

  /* Fill in the location this expression names: recurse into the container it reaches through,
   * then append the key step it takes.  A root fills in its variable handle and no steps.  FALSE
   * means the value has no address -- a macro, a function result, a literal -- and, since every
   * assignment forks its RHS, nothing can alias it either, so a mutator may ignore it.
   *
   * Only ever reached through filterx_expr_get_path(), which is what zeroes @path_out; a node
   * must not call another node's hook directly.
   *
   * Outside the JIT guard for the same reason as static_type, plus one of its own: paths are
   * JIT-independent and test_type_path.c exercises them in the --disable-jit build. */
  gboolean (*get_path)(FilterXExpr *self, FilterXTypePath *path_out);

  void (*free_fn)(FilterXExpr *self);

  gboolean (*walk_children)(FilterXExpr *self, FilterXExprWalkFunc f, gpointer user_data);

  /* type of the expr, is not freed, assumed to be managed by something else
   * */

  const gchar *type;

  /* name associated with the expr (e.g.  function name), is not freed by
   * FilterXExpr, assumed to be managed by something else */
  const gchar *name;
  CFG_LTYPE *lloc;
  gchar *expr_text;
};

#define FILTERX_EXPR_TYPE_NAME(_type) filterx_expr_type_ ## _type

#define FILTERX_EXPR_DECLARE_TYPE(_type) \
  extern const gchar *FILTERX_EXPR_TYPE_NAME(_type);

#define FILTERX_EXPR_DEFINE_TYPE(_type) \
  const gchar *FILTERX_EXPR_TYPE_NAME(_type) = # _type

void _filterx_expr_propagate_to_error(FilterXExpr *self);

/*
 * Evaluate the expression and return the result as a FilterXObject.  The
 * result can either be a
 *
 *   1) raw representation (e.g.  a marshalled series of bytes + syslog-ng
 *      type hint encapsulated into a FilterXMessageValue)
 *
 *   2) typed representation (e.g.  a demarshalled object, something other
 *      than FilterXMessageValue, like FilterXJSON)
 *
 * If the caller is not ok with handling the raw representation, just use
 * filterx_expr_eval_typed() which will unmarshall any values before
 * returning them.
 */
static inline FilterXObject *
filterx_expr_eval(FilterXExpr *self)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(self->inited);
#endif

  stats_counter_inc(self->eval_count);

  FilterXObject *result = self->eval(self);
  if (!result)
    {
      _filterx_expr_propagate_to_error(self);
      return NULL;
    }
  return result;
}

static inline gboolean
filterx_expr_has_effect(FilterXExpr *expr, FilterXEffect effect)
{
  return !!(expr->effects & effect);
}

/* TODO: this should be in FilterXObject, _update_repr() prevents that */
static inline FilterXObject *
filterx_expr_make_typed_object(FilterXExpr *self, FilterXObject *obj)
{
  if (!obj)
    return NULL;

  FilterXObject *unmarshalled = filterx_object_unmarshal(obj);

  if (!unmarshalled)
    {
      filterx_object_unref(obj);
      return NULL;
    }

  if (obj == unmarshalled)
    {
      filterx_object_unref(unmarshalled);
      return obj;
    }

  filterx_object_unref(obj);
  if (self->_update_repr)
    self->_update_repr(self, &unmarshalled);

  return unmarshalled;
}

/*
 * Evaluate the expression and return the result as a typed FilterXObject.
 * This function will call filterx_expr_eval() and then unmarshal the result
 * so the result is always a typed object.
 */
static inline FilterXObject *
filterx_expr_eval_typed(FilterXExpr *self)
{
  FilterXObject *result = filterx_expr_eval(self);
  return filterx_expr_make_typed_object(self, result);
}

static inline gboolean
filterx_expr_assign(FilterXExpr *self, FilterXObject **new_value)
{
  if (self->assign)
    return self->assign(self, new_value);
  return FALSE;
}

static inline FilterXObject *
filterx_expr_plus_assign(FilterXExpr *self, FilterXObject *value)
{
  if (self->plus_assign)
    return self->plus_assign(self, value);
  return NULL;
}

static inline gboolean
filterx_expr_is_set(FilterXExpr *self)
{
  if (self->is_set)
    return self->is_set(self);
  return FALSE;
}

static inline gboolean
filterx_expr_unset(FilterXExpr *self)
{
  if (self->unset)
    return self->unset(self);
  return FALSE;
}

static inline FilterXObject *
filterx_expr_move(FilterXExpr *self)
{
  if (self->move)
    return self->move(self);
  return NULL;
}

static inline gboolean
filterx_expr_unset_available(FilterXExpr *self)
{
  return self->unset != NULL;
}


/* move() method is always available a default implementation is derived
 * from FilterXExpr, but we also need the unset() method in the generic one. */
static inline gboolean
filterx_expr_move_available(FilterXExpr *self)
{
  return filterx_expr_unset_available(self) && self->move != NULL;
}

void filterx_expr_set_location(FilterXExpr *self, CfgLexer *lexer, CFG_LTYPE *lloc);
void filterx_expr_set_location_with_text(FilterXExpr *self, CFG_LTYPE *lloc, const gchar *text);
EVTTAG *filterx_expr_format_location_tag(FilterXExpr *self);
GString *filterx_expr_format_location(FilterXExpr *self);
const gchar *filterx_expr_get_text(FilterXExpr *self);
FilterXExpr *filterx_expr_optimize(FilterXExpr *self);
void filterx_expr_init_instance(FilterXExpr *self, const gchar *type, FilterXEffect effects);
FilterXExpr *filterx_expr_new(void);
FilterXExpr *filterx_expr_ref(FilterXExpr *self);
void filterx_expr_unref(FilterXExpr *self);
FilterXObject *filterx_expr_plus_assign_method(FilterXExpr *self, FilterXObject *value);
void filterx_expr_free_method(FilterXExpr *self);

gboolean filterx_expr_init_method(FilterXExpr *self, GlobalConfig *cfg);
void filterx_expr_deinit_method(FilterXExpr *self, GlobalConfig *cfg);

gboolean filterx_expr_init(FilterXExpr *self, GlobalConfig *cfg);
void filterx_expr_deinit(FilterXExpr *self, GlobalConfig *cfg);

void filterx_expr_infer_types_default(FilterXExpr *self, FilterXTypeEnv *env);

static inline gboolean
filterx_expr_visit(FilterXExpr *self, FilterXExpr **expr, FilterXExprWalkFunc f, gpointer user_data)
{
  return f(self, expr, user_data);
}

static inline gboolean
filterx_expr_walk_children(FilterXExpr *self, FilterXExprWalkFunc f, gpointer user_data)
{
  if (!self)
    return TRUE;

  g_assert(self->walk_children);

  return self->walk_children(self, f, user_data);
}

/* Peel @self down to the location it names.  Unlike walk_children() a missing hook is not an
 * assert but the answer itself: most expressions are not addressable. */
static inline gboolean
filterx_expr_get_path(FilterXExpr *self, FilterXTypePath *path_out)
{
  memset(path_out, 0, sizeof(*path_out));

  if (!self || !self->get_path)
    return FALSE;

  return self->get_path(self, path_out);
}

/* TODO partialJIT: remove once all expressions implement compile() */
static inline gboolean
filterx_expr_can_compile(FilterXExpr *self)
{
#if SYSLOG_NG_ENABLE_JIT
  return self && !!self->compile;
#else
  return FALSE;
#endif
}

static inline FilterXIRValue
filterx_expr_compile(FilterXExpr *self, FilterXJIT *jit)
{
#if SYSLOG_NG_ENABLE_JIT
  g_assert(self && self->compile);
  g_assert(self->types_inferred);   /* an un-inferred node reads as UNKNOWN and never devirtualizes */

  FilterXIRValue result = self->compile(self, jit);

  return fx_jit_emit_expr_propagate_to_error_if_null(jit, self, result);
#else
  g_assert_not_reached();
#endif
}

/* TODO partialJIT: remove once all expressions implement compile_assign() */
static inline gboolean
filterx_expr_can_compile_assign(FilterXExpr *self)
{
#if SYSLOG_NG_ENABLE_JIT
  return self && !!self->compile_assign;
#else
  return FALSE;
#endif
}

static inline FilterXIRValue
filterx_expr_compile_assign(FilterXExpr *self, FilterXJIT *jit, FilterXIRValue new_value)
{
#if SYSLOG_NG_ENABLE_JIT
  g_assert(self && self->compile_assign);
  g_assert(self->types_inferred);   /* an un-inferred node reads as UNKNOWN and never devirtualizes */

  return self->compile_assign(self, jit, new_value);
#else
  g_assert_not_reached();
#endif
}

static inline FilterXIRValue
filterx_expr_compile_typed(FilterXExpr *self, FilterXJIT *jit)
{
#if SYSLOG_NG_ENABLE_JIT
  g_assert(self && self->compile);
  g_assert(self->types_inferred);   /* an un-inferred node reads as UNKNOWN and never devirtualizes */

  FilterXIRValue result = self->compile(self, jit);

  return fx_jit_emit_expr_make_typed_object(jit, self, result);
#else
  g_assert_not_reached();
#endif
}

/* TODO partialJIT: remove once all expressions implement compile() */
static inline FilterXIRValue
filterx_expr_compile_or_eval(FilterXExpr *self, FilterXJIT *jit)
{
  if (filterx_expr_can_compile(self))
    return filterx_expr_compile(self, jit);

  return fx_jit_emit_expr_eval(jit, self);
}

static inline FilterXIRValue
filterx_expr_compile_or_eval_typed(FilterXExpr *self, FilterXJIT *jit)
{
  if (filterx_expr_can_compile(self))
    return filterx_expr_compile_typed(self, jit);

  return fx_jit_emit_expr_eval_typed(jit, self);
}

typedef struct _FilterXUnaryOp
{
  FilterXExpr super;
  FilterXExpr *operand;
} FilterXUnaryOp;

gboolean filterx_unary_op_init_method(FilterXExpr *s, GlobalConfig *cfg);
void filterx_unary_op_deinit_method(FilterXExpr *s, GlobalConfig *cfg);
void filterx_unary_op_free_method(FilterXExpr *s);
void filterx_unary_op_init_instance(FilterXUnaryOp *self, const gchar *name, FilterXEffect effects,
                                    FilterXExpr *operand);

typedef struct _FilterXBinaryOp
{
  FilterXExpr super;
  FilterXExpr *lhs, *rhs;
} FilterXBinaryOp;

gboolean filterx_binary_op_init_method(FilterXExpr *s, GlobalConfig *cfg);
void filterx_binary_op_deinit_method(FilterXExpr *s, GlobalConfig *cfg);
void filterx_binary_op_free_method(FilterXExpr *s);
void filterx_binary_op_init_instance(FilterXBinaryOp *self, const gchar *name, FilterXEffect effects,
                                     FilterXExpr *lhs, FilterXExpr *rhs);

#endif

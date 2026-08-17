/*
 * Copyright (c) 2026 Axoflow
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
#ifndef FILTERX_TYPE_INFERENCE_H_INCLUDED
#define FILTERX_TYPE_INFERENCE_H_INCLUDED

#include "syslog-ng.h"
#include "filterx/filterx-variable.h"

/* What an expression's value is, when it evaluates to one at all.  Flat, because the nesting an
 * expression reaches through already lives in the expression tree: d.a.b is
 * getattr(getattr(var d, "a"), "b") and every node gets its own kind from its own path. */
typedef enum
{
  FILTERX_STATIC_TYPE_UNKNOWN = 0,
  FILTERX_STATIC_TYPE_DICT    = 1,
  FILTERX_STATIC_TYPE_LIST    = 2,
  FILTERX_STATIC_TYPE_STRING  = 3,
  FILTERX_STATIC_TYPE_INTEGER = 4,
  FILTERX_STATIC_TYPE_DOUBLE  = 5,
  FILTERX_STATIC_TYPE_BOOLEAN = 6,
} FilterXStaticType;

static inline FilterXStaticType
filterx_static_type_meet(FilterXStaticType a, FilterXStaticType b)
{
  return a == b ? a : FILTERX_STATIC_TYPE_UNKNOWN;
}

FilterXStaticType filterx_static_type_numeric_promote(FilterXStaticType a, FilterXStaticType b);

/* One GTree per env, keyed by a path, holding a flat fact: a kind plus a `closed` bit.
 *
 *   entry at P      the value at P exists and has this kind
 *   no entry at P   nothing is known about whether P exists
 *   `closed` at P   P is a container with no keys beyond the child paths recorded here
 *
 * There is nothing else: a lookup either hits an entry that speaks for exactly that location, or
 * misses and answers UNKNOWN.  Nothing is summarised over the keys this pass cannot name, which
 * is what makes a hit knowledge rather than a bound -- and what a write through such a key costs:
 * it may have landed anywhere in its container, so the container keeps its kind and loses its
 * whole interior.  `d = {}; d[$k] = "s";` leaves d a DICT with no leaves, and the `d.a = 1` after
 * it is then simply what d.a is.
 *
 * A read only ever describes a non-NULL eval result -- every in-tree container returns C NULL for
 * a missing key rather than a null object -- which is what lets a key written on one branch of an
 * if/else keep its type past the join.
 */
typedef struct _FilterXTypeEnv FilterXTypeEnv;

FilterXTypeEnv *filterx_type_env_new(void);
FilterXTypeEnv *filterx_type_env_clone(const FilterXTypeEnv *self);
void filterx_type_env_free(FilterXTypeEnv *self);

typedef gboolean (*FilterXTypeEnvHandlePredicate)(FilterXVariableHandle handle, gpointer user_data);

FilterXTypeEnv *filterx_type_env_clone_filtered(const FilterXTypeEnv *self,
                                                FilterXTypeEnvHandlePredicate pred, gpointer user_data);

struct _FilterXExpr;
/* Defined in filterx-expr.h, which includes this header: an expression is what mints a path, so
 * the type lives with the expression tree and only pointers to it cross to this side. */
struct _FilterXAccessPath;

/* TRUE when @path has an entry of its own, which is a claim that the value exists, plus its
 * `closed` bit.  Recorded-at-UNKNOWN and unrecorded read the same through get_at_path() and are
 * not the same fact: the join turns on exactly that difference. */
gboolean filterx_type_env_get_fact_at_path(const FilterXTypeEnv *self, const struct _FilterXAccessPath *path,
                                 FilterXStaticType *kind_out, gboolean *closed_out);

void filterx_type_env_set_at_path(FilterXTypeEnv *self, const struct _FilterXAccessPath *path,
                                  FilterXStaticType kind, gboolean closed);

/* Record that @path now holds what @rhs_expr evaluates to.  A literal, a partially evaluated
 * literal container and a read of another tracked location carry a shape, which is installed
 * below @path; anything else installs its flat kind and nothing under it.
 *
 * A truncated @path names no location to install anything at, so the deepest addressable ancestor
 * of the write is opened instead. */
void filterx_type_env_set_shape_at_path(FilterXTypeEnv *self, const struct _FilterXAccessPath *path,
                                        struct _FilterXExpr *rhs_expr);

/* Something mutated the container at @path in a way this pass cannot follow: keep its kind, drop
 * everything below it and stop claiming its key set is complete. */
void filterx_type_env_open_at_path(FilterXTypeEnv *self, const struct _FilterXAccessPath *path);

/* Retire what is known about @path, which unset() and move() have proven gone.  The parent keeps
 * `closed`: that is an upper bound on its key set, and a removal only ever shrinks one. */
void filterx_type_env_clear_at_path(FilterXTypeEnv *self, const struct _FilterXAccessPath *path);

/* Convenience for the read hooks: peel @expr to a path and read it. */
FilterXStaticType filterx_type_env_get_static_type_of_expr(const FilterXTypeEnv *self, struct _FilterXExpr *expr);

void filterx_type_env_update_on_remove(FilterXTypeEnv *self, struct _FilterXExpr *target_expr);

FilterXStaticType filterx_type_env_update_on_plus_assign(FilterXTypeEnv *self,
                                                            struct _FilterXExpr *target_expr,
                                                            struct _FilterXExpr *rhs_expr);

/* Every container an expression can reach may have been mutated in place by a callee, so open
 * each argument that names a tracked location. */
void filterx_type_env_open_arguments(FilterXTypeEnv *self, struct _FilterXExpr *expr);

void filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src);

void filterx_expr_infer_types(struct _FilterXExpr *self, FilterXTypeEnv *env);

#endif

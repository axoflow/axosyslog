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
 * getattr(getattr(var d, "a"), "b") and every node can answer for its own value alone. */
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

/* Two kinds agree or they do not; there is no partial agreement to keep. */
static inline FilterXStaticType
filterx_static_type_meet(FilterXStaticType a, FilterXStaticType b)
{
  return a == b ? a : FILTERX_STATIC_TYPE_UNKNOWN;
}

FilterXStaticType filterx_static_type_numeric_promote(FilterXStaticType a, FilterXStaticType b);

/* What is known about the variables in scope at one point of the program.  The env is the only
 * place a fact about a *location* lives; an expression carries only the kind of its own value. */
typedef struct _FilterXTypeEnv FilterXTypeEnv;

FilterXTypeEnv *filterx_type_env_new(void);
FilterXTypeEnv *filterx_type_env_clone(const FilterXTypeEnv *self);
void filterx_type_env_free(FilterXTypeEnv *self);

typedef gboolean (*FilterXTypeEnvHandlePredicate)(FilterXVariableHandle handle, gpointer user_data);

FilterXTypeEnv *filterx_type_env_clone_filtered(const FilterXTypeEnv *self,
                                                FilterXTypeEnvHandlePredicate pred, gpointer user_data);

struct _FilterXExpr;

/* What the location @expr reads holds, or UNKNOWN when it names no location this pass tracks. */
FilterXStaticType filterx_type_env_get_static_type_of_expr(const FilterXTypeEnv *self, struct _FilterXExpr *expr);

void filterx_type_env_update_on_remove(FilterXTypeEnv *self, struct _FilterXExpr *target_expr);

FilterXStaticType filterx_type_env_update_on_plus_assign(FilterXTypeEnv *self,
                                                            struct _FilterXExpr *target_expr,
                                                            struct _FilterXExpr *rhs_expr);

/* Every container an expression can reach may have been mutated in place by a callee. */
void filterx_type_env_open_arguments(FilterXTypeEnv *self, struct _FilterXExpr *expr);

/* Join: keep only what holds on both paths. */
void filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src);

void filterx_expr_infer_types(struct _FilterXExpr *self, FilterXTypeEnv *env);

#endif

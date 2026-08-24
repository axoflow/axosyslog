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
#include "filterx/filterx-object.h"
#include "filterx/filterx-access-path.h"

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
FilterXStaticType filterx_static_type_from_object(FilterXObject *obj);

/* What is known about the locations a block can reach.  Keyed by path with no inheritance: an entry
 * speaks for its own location only, and `closed` says the recorded children are all the keys that
 * container has.  Every in-tree container returns C NULL for a missing key, so an entry only ever
 * describes a non-NULL eval result. */
typedef struct _FilterXTypeEnv FilterXTypeEnv;

FilterXTypeEnv *filterx_type_env_new(void);
FilterXTypeEnv *filterx_type_env_clone(const FilterXTypeEnv *self);
void filterx_type_env_free(FilterXTypeEnv *self);

void filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src);

/* Record that @path now holds what @rhs_expr evaluates to. */
void filterx_type_env_set_shape_at_path(FilterXTypeEnv *self, const FilterXAccessPath *path,
                                        FilterXExpr *rhs_expr);

FilterXStaticType filterx_type_env_get_static_type_of_expr(const FilterXTypeEnv *self, FilterXExpr *expr);

void filterx_type_env_update_on_write(FilterXTypeEnv *self, FilterXExpr *target_expr, FilterXExpr *rhs_expr);

/* `??=` writes only when the target is null, so what holds afterwards is the meet of having
 * written and of not having written. */
void filterx_type_env_update_on_optional_write(FilterXTypeEnv *self, FilterXExpr *target_expr,
                                               FilterXExpr *rhs_expr);

void filterx_type_env_update_on_remove(FilterXTypeEnv *self, FilterXExpr *target_expr);

FilterXStaticType filterx_type_env_update_on_plus_assign(FilterXTypeEnv *self, FilterXExpr *target_expr,
                                                         FilterXExpr *rhs_expr);

/* a callee may mutate in place any container it was handed */
void filterx_type_env_open_arguments(FilterXTypeEnv *self, FilterXExpr *expr);

void filterx_expr_infer_types(FilterXExpr *self, FilterXTypeEnv *env);

/* SYSLOG_NG_FILTERX_TRACE_TYPES enables the dump of the pass to stderr */
gboolean filterx_type_inference_trace_enabled(void);

/* no-ops unless the trace is enabled */
void filterx_type_inference_trace_banner(const gchar *format, ...) G_GNUC_PRINTF(1, 2);
void filterx_type_env_trace_dump(const FilterXTypeEnv *self, const gchar *label);

#endif

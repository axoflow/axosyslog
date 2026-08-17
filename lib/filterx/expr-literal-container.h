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

#ifndef FILTERX_EXPR_LITERAL_H_INCLUDED
#define FILTERX_EXPR_LITERAL_H_INCLUDED

#include "filterx/filterx-expr.h"


typedef struct FilterXLiteralElement_ FilterXLiteralElement;
typedef struct FilterXLiteralContainer_ FilterXLiteralContainer;

FilterXLiteralElement *filterx_literal_element_new(FilterXExpr *key, FilterXExpr *value);
FilterXLiteralElement *filterx_nullv_literal_element_new(FilterXExpr *key, FilterXExpr *value);

/* Literal Object expressions */

FILTERX_EXPR_DECLARE_TYPE(literal_container);

gsize filterx_literal_container_len(FilterXExpr *s);

/* The container the optimizer already materialised: every literal element in place, every
 * non-literal one a null-valued hole.  NULL when the container could not be evaluated early at
 * all -- a non-literal key fails filterx_mapping_normalize_key() and bails out of the whole early
 * eval, so a template at all means every key of it is a literal string. */
FilterXObject *filterx_literal_container_get_template(FilterXExpr *s);

/* Type the holes the template left, at @base in @env.
 *
 * Returns FALSE when the container holds a key the caller cannot have recorded, which is a reason
 * for @base to stop claiming a complete key set.  Two elements do that: one at a key nothing can
 * name, and a nullv one -- its slot is left unset when its value is null, so an entry would
 * falsely assert the key exists while the key may turn up all the same. */
gboolean filterx_literal_container_overlay_hole_types(FilterXExpr *s, FilterXTypeEnv *env,
                                                      const FilterXTypePath *base);


/* Literal Dict */

FILTERX_EXPR_DECLARE_TYPE(literal_dict);

typedef gboolean (*FilterXLiteralDictForeachFunc)(FilterXExpr *, FilterXExpr *, gpointer);
gboolean filterx_literal_dict_foreach(FilterXExpr *s, FilterXLiteralDictForeachFunc func, gpointer user_data);
FilterXExpr *filterx_literal_dict_new(GList *elements);

/* Literal List */

FILTERX_EXPR_DECLARE_TYPE(literal_list);

typedef gboolean (*FilterXLiteralListForeachFunc)(gsize, FilterXExpr *, gpointer);

gboolean filterx_literal_list_foreach(FilterXExpr *s, FilterXLiteralListForeachFunc func, gpointer user_data);

FilterXExpr *filterx_literal_list_new(GList *elements);

/* Literal Tuple */

FILTERX_EXPR_DECLARE_TYPE(literal_tuple);

FilterXExpr *filterx_literal_tuple_new(GList *elements);

/* inline functions */

static inline gboolean
filterx_expr_is_literal_dict(FilterXExpr *expr)
{
  return expr && expr->type == FILTERX_EXPR_TYPE_NAME(literal_dict);
}

static inline gboolean
filterx_expr_is_literal_list(FilterXExpr *expr)
{
  return expr && expr->type == FILTERX_EXPR_TYPE_NAME(literal_list);
}

static inline gboolean
filterx_expr_is_literal_tuple(FilterXExpr *expr)
{
  return expr && expr->type == FILTERX_EXPR_TYPE_NAME(literal_tuple);
}

static inline gboolean
filterx_expr_is_literal_container(FilterXExpr *s)
{
  return filterx_expr_is_literal_list(s) || filterx_expr_is_literal_dict(s) || filterx_expr_is_literal_tuple(s);
}

#endif

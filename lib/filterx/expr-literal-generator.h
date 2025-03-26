/*
 * Copyright (c) 2024 Attila Szakacs
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef FILTERX_EXPR_LITERAL_GENERATOR_H_INCLUDED
#define FILTERX_EXPR_LITERAL_GENERATOR_H_INCLUDED

#include "filterx/filterx-expr.h"


typedef struct FilterXLiteralElement_ FilterXLiteralElement;
typedef struct FilterXLiteralContainer_ FilterXLiteralContainer;

FilterXLiteralElement *filterx_literal_element_new(FilterXExpr *key, FilterXExpr *value,
    gboolean cloneable);

/* Literal Object expressions */

FILTERX_EXPR_DECLARE_TYPE(literal_container);
   
gsize filterx_literal_container_len(FilterXExpr *s);


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
filterx_expr_is_literal_container(FilterXExpr *s)
{
  return filterx_expr_is_literal_list(s) || filterx_expr_is_literal_dict(s);
}

#endif

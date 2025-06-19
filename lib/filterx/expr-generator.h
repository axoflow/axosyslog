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

#ifndef FILTERX_EXPR_GENERATOR_H_INCLUDED
#define FILTERX_EXPR_GENERATOR_H_INCLUDED

#include "filterx/filterx-expr.h"

typedef struct FilterXExprGenerator_ FilterXExprGenerator;
struct FilterXExprGenerator_
{
  FilterXExpr super;
  FilterXExpr *fillable;
  gboolean (*generate)(FilterXExprGenerator *self, FilterXObject *fillable);
  FilterXObject *(*create_container)(FilterXExprGenerator *self, FilterXExpr *fillable_parent);
};

void filterx_generator_set_fillable(FilterXExpr *s, FilterXExpr *fillable);
void filterx_generator_init_instance(FilterXExpr *s);
FilterXExpr *filterx_generator_optimize_method(FilterXExpr *s);
gboolean filterx_generator_init_method(FilterXExpr *s, GlobalConfig *cfg);
void filterx_generator_deinit_method(FilterXExpr *s, GlobalConfig *cfg);
void filterx_generator_free_method(FilterXExpr *s);

FILTERX_EXPR_DECLARE_TYPE(generator);

FilterXExpr *filterx_generator_create_container_new(FilterXExpr *g, FilterXExpr *fillable_parent);

static inline gboolean
filterx_expr_is_generator(FilterXExpr *expr)
{
  return expr && expr->type == FILTERX_EXPR_TYPE_NAME(generator);
}

/* protected */
FilterXObject *filterx_generator_create_dict_container(FilterXExprGenerator *s, FilterXExpr *fillable_parent);
FilterXObject *filterx_generator_create_list_container(FilterXExprGenerator *s, FilterXExpr *fillable_parent);

#endif

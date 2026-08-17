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

#include "filterx/filterx-type-inference.h"
#include "filterx/filterx-expr.h"

/* Placeholder: implemented by a later change. */
FilterXStaticType
filterx_static_type_numeric_add_result(FilterXStaticType a, FilterXStaticType b)
{
  return FILTERX_STATIC_TYPE_UNKNOWN;
}

struct _FilterXTypeEnv
{
  /* Placeholder: implemented by a later change. */
  gint dummy;
};

FilterXTypeEnv *
filterx_type_env_new(void)
{
  return g_new0(FilterXTypeEnv, 1);
}

/* Placeholder: implemented by a later change. */
FilterXTypeEnv *
filterx_type_env_clone(const FilterXTypeEnv *self)
{
  return g_new0(FilterXTypeEnv, 1);
}

/* Placeholder: implemented by a later change. */
FilterXTypeEnv *
filterx_type_env_clone_filtered(const FilterXTypeEnv *self, FilterXTypeEnvHandlePredicate pred, gpointer user_data)
{
  return g_new0(FilterXTypeEnv, 1);
}

void
filterx_type_env_free(FilterXTypeEnv *self)
{
  g_free(self);
}

/* Placeholder: implemented by a later change. */
FilterXStaticType
filterx_type_env_get_for_expr(const FilterXTypeEnv *self, FilterXExpr *expr)
{
  return FILTERX_STATIC_TYPE_UNKNOWN;
}

/* Placeholder: implemented by a later change. */
void
filterx_type_env_update_on_remove(FilterXTypeEnv *self, FilterXExpr *target_expr)
{
}

/* Placeholder: implemented by a later change. */
FilterXStaticType
filterx_type_env_update_on_inplace_modify(FilterXTypeEnv *self, FilterXExpr *target_expr, FilterXExpr *rhs_expr)
{
  return FILTERX_STATIC_TYPE_UNKNOWN;
}

/* Placeholder: implemented by a later change. */
void
filterx_type_env_invalidate_arguments(FilterXTypeEnv *self, FilterXExpr *expr)
{
}

/* Placeholder: implemented by a later change. */
void
filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src)
{
}

void
filterx_expr_infer_types(FilterXExpr *self, FilterXTypeEnv *env)
{
#if SYSLOG_NG_ENABLE_JIT
  if (!self)
    return;

  if (self->infer_types)
    self->infer_types(self, env);
  else
    filterx_expr_infer_types_default(self, env);
#endif
}

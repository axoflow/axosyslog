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

/* The canonical "no information" spec. It is the single representation of UNKNOWN: specs are
 * never NULL. Its element points at itself, so it terminates every element chain and survives
 * any depth walk. */
const struct _FilterXStaticTypeNode FILTERX_STATIC_TYPE_UNKNOWN_NODE =
{
  .kind    = FILTERX_STATIC_TYPE_UNKNOWN,
  .element = &FILTERX_STATIC_TYPE_UNKNOWN_NODE,
};

struct _FilterXTypeEnv
{
  /* Placeholder: the type environment is wired up by a later change. */
  gint dummy;
};

/* Placeholder: container freezing is implemented by a later change. */
FilterXStaticTypeSpec
filterx_static_type_make_container(FilterXStaticType kind, FilterXStaticTypeSpec element)
{
  return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
}

/* Placeholder: kind freezing is implemented by a later change. */
FilterXStaticTypeSpec
filterx_static_type_kind_only(FilterXStaticType kind)
{
  return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
}

/* Placeholder: the per-level meet lattice is implemented by a later change. */
FilterXStaticTypeSpec
filterx_static_type_spec_meet(FilterXStaticTypeSpec a, FilterXStaticTypeSpec b)
{
  return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
}

FilterXTypeEnv *
filterx_type_env_new(void)
{
  return g_new0(FilterXTypeEnv, 1);
}

FilterXTypeEnv *
filterx_type_env_clone(const FilterXTypeEnv *self)
{
  return g_new0(FilterXTypeEnv, 1);
}

void
filterx_type_env_free(FilterXTypeEnv *self)
{
  g_free(self);
}

/* Placeholder: variable type lookup is implemented by a later change. */
FilterXStaticTypeSpec
filterx_type_env_get(const FilterXTypeEnv *self, FilterXVariableHandle handle)
{
  return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
}

/* Placeholder: variable type recording is implemented by a later change. */
void
filterx_type_env_set(FilterXTypeEnv *self, FilterXVariableHandle handle, FilterXStaticTypeSpec spec)
{
}

/* Placeholder: branch-join meet is implemented by a later change. */
void
filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src)
{
}

void
filterx_expr_infer_types(FilterXExpr *self, FilterXTypeEnv *env)
{
  if (!self)
    return;

  if (self->infer_types)
    self->infer_types(self, env);
  else
    filterx_expr_infer_types_default(self, env);
}

void
filterx_expr_infer_types_root(FilterXExpr *root)
{
  if (!root)
    return;
  FilterXTypeEnv *env = filterx_type_env_new();
  filterx_expr_infer_types(root, env);
  filterx_type_env_free(env);
}

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

typedef enum
{
  /* "no information" — the kind of the canonical unknown singleton spec
   * (FILTERX_STATIC_TYPE_UNKNOWN_SPEC). Every enum value is a legal node kind. */
  FILTERX_STATIC_TYPE_UNKNOWN = 0,
  FILTERX_STATIC_TYPE_DICT    = 1,
  FILTERX_STATIC_TYPE_LIST    = 2,
  FILTERX_STATIC_TYPE_STRING  = 3,
  FILTERX_STATIC_TYPE_INTEGER = 4,
} FilterXStaticType;

/* FilterXStaticTypeSpec models the static type of an expression as a linear chain of
 * nesting levels: the outer kind, then the element type of that container, then the
 * element type of that, and so on. Each container level has exactly one element type
 * (heterogeneous elements meet to UNKNOWN), so the structure is a chain, not a tree.
 *
 * The chain is represented as frozen / hash-consed immutable nodes: structurally-equal
 * chains are the SAME pointer, so equality is pointer identity (==) and a copy is just a
 * pointer copy. A spec is NEVER NULL: the absent / "no information" type is the canonical
 * unknown singleton FILTERX_STATIC_TYPE_UNKNOWN_SPEC, which also terminates every element
 * chain. There is no depth limit. Frozen nodes live in a process-global, never-freed pool,
 * so a spec cached on a FilterXExpr stays valid for as long as that expression does. */
typedef const struct _FilterXStaticTypeNode *FilterXStaticTypeSpec;

struct _FilterXStaticTypeNode
{
  FilterXStaticType kind;          /* outer kind of this chain (UNKNOWN only for the unknown singleton) */
  FilterXStaticTypeSpec element;    /* frozen element chain (never NULL; UNKNOWN-terminated) */
};

/* The canonical "no information" spec: a single pre-seeded node whose kind is UNKNOWN and
 * whose element points at itself, so it terminates every element chain and can be walked to
 * any depth. Specs are never NULL — this object stands in for "absent type". */
extern const struct _FilterXStaticTypeNode FILTERX_STATIC_TYPE_UNKNOWN_NODE;
#define FILTERX_STATIC_TYPE_UNKNOWN_SPEC (&FILTERX_STATIC_TYPE_UNKNOWN_NODE)
#define INITIAL_FILTERX_STATIC_TYPE_SPEC FILTERX_STATIC_TYPE_UNKNOWN_SPEC

static inline FilterXStaticType
filterx_static_type_kind(FilterXStaticTypeSpec spec)
{
  return spec->kind;
}

/* Drop the outermost level: returns the element type spec.
 * Element of DICT/LIST → its element chain; element of a scalar or UNKNOWN → UNKNOWN. */
static inline FilterXStaticTypeSpec
filterx_static_type_element(FilterXStaticTypeSpec spec)
{
  FilterXStaticType kind = filterx_static_type_kind(spec);
  if (kind != FILTERX_STATIC_TYPE_DICT && kind != FILTERX_STATIC_TYPE_LIST)
    return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
  return spec->element;
}

/* Build (freeze) a container spec from an outer kind and the element spec. */
FilterXStaticTypeSpec filterx_static_type_make_container(FilterXStaticType kind, FilterXStaticTypeSpec element);

/* Per-level meet (frozen): the longest common prefix where the kinds agree. */
FilterXStaticTypeSpec filterx_static_type_spec_meet(FilterXStaticTypeSpec a, FilterXStaticTypeSpec b);

/* Convenience for the common case where we just need the outer kind: a single-level
 * frozen chain. Returns the UNKNOWN singleton for kind == UNKNOWN. */
FilterXStaticTypeSpec filterx_static_type_kind_only(FilterXStaticType kind);

typedef struct _FilterXTypeEnv FilterXTypeEnv;

FilterXTypeEnv *filterx_type_env_new(void);
FilterXTypeEnv *filterx_type_env_clone(const FilterXTypeEnv *self);
void filterx_type_env_free(FilterXTypeEnv *self);

FilterXStaticTypeSpec filterx_type_env_get(const FilterXTypeEnv *self, FilterXVariableHandle handle);
void filterx_type_env_set(FilterXTypeEnv *self, FilterXVariableHandle handle, FilterXStaticTypeSpec spec);

/* Meet @src into @dst in place. Handles only in one env meet with all-UNKNOWN
 * → all-UNKNOWN (which is the absent-key default). */
void filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src);

struct _FilterXExpr;
void filterx_expr_infer_types(struct _FilterXExpr *self, FilterXTypeEnv *env);
void filterx_expr_infer_types_root(struct _FilterXExpr *root);

#endif

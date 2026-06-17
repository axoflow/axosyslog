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
  /* Sentinel for "freshly built empty container, element type not yet committed".
   * Seeded by literal inference for empty {} / [] containers and carried through the
   * env, where the first write to the container lifts it to the written value's type
   * (see filterx_type_env_update_on_write). It must never reach codegen as if it were
   * a real type: it is stripped from any expression's exposed static_type by
   * filterx_static_type_sanitize() (used by element() and by variable inference).
   * Devirt decisions only read level 0 (filterx_static_type_kind), which is never FRESH. */
  FILTERX_STATIC_TYPE_FRESH   = 0xfe,
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
  FilterXStaticTypeSpec element;    /* frozen element chain (never NULL; UNKNOWN-terminated); may carry FRESH inside */
  FilterXStaticTypeSpec sanitized;  /* this chain truncated at the first FRESH level (precomputed) */
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

/* Strip the FRESH lift-tracking sentinel before a spec is exposed on any expression's
 * static_type. Placeholder: the real stripping is implemented by a later change; since
 * the FRESH sentinel is only produced once the nested inference hooks are wired up, the
 * identity behaviour here matches what callers observe so far. */
static inline FilterXStaticTypeSpec
filterx_static_type_sanitize(FilterXStaticTypeSpec spec)
{
  return spec;
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

/* Per-key attribute type tracking: resolve or record the type of <chain_expr>.<key> where
 * chain_expr is a getattr chain rooted at a variable.  The path "root_handle:k0:k1:...:key"
 * is stored in the attr_to_spec table.  Tracks individual field types for heterogeneous-value
 * dicts (e.g. log.observed_time → INTEGER, log.severity_text → STRING) without smearing, and
 * covers the single-hop case (variable.key) as a zero-prefix chain.  A no-op when the chain is
 * too deep or contains non-getattr hops. */
FilterXStaticTypeSpec filterx_type_env_get_attr_for_chain(const FilterXTypeEnv *self,
                                                          struct _FilterXExpr *chain_expr,
                                                          const gchar *key);
void filterx_type_env_meet_attr_for_chain(FilterXTypeEnv *self,
                                          struct _FilterXExpr *chain_expr,
                                          const gchar *key, FilterXStaticTypeSpec spec);

/* Meet @src into @dst in place. Handles only in one env meet with all-UNKNOWN
 * → all-UNKNOWN (which is the absent-key default). */
void filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src);

struct _FilterXExpr;
void filterx_expr_infer_types(struct _FilterXExpr *self, FilterXTypeEnv *env);
void filterx_expr_infer_types_root(struct _FilterXExpr *root);

/* Update the env after a write `container_expr.<key> = value` (setattr / set_subscript).
 * Walks @container_expr down to its root variable, counting the getattr/get_subscript depth
 * d, and refines the variable's spec at level d+1 using the lift-or-meet rule:
 *
 *   prior level kind = FRESH    → lift: commit the level to @value_spec (the just-written
 *                                 value's full nested type, including its own FRESH).
 *   prior level kind = known T  → meet(T-tail, value_spec): divergent kinds collapse to
 *                                 UNKNOWN, demoting a now-heterogeneous container.
 *   prior level kind = UNKNOWN  → leave (no committed type to refine from).
 *
 * A no-op if @container_expr is not rooted at a trackable variable or if the root has no
 * known container kind at the written level. */
void filterx_type_env_update_on_write(FilterXTypeEnv *env,
                                      struct _FilterXExpr *container_expr,
                                      FilterXStaticTypeSpec value_spec);

#endif

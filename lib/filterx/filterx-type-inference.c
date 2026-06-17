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

/* The canonical "no information" spec. It lives outside the pool (it has no concrete kind to
 * key on) and is the single representation of UNKNOWN: specs are never NULL. Its element points
 * at itself, so it terminates every element chain and survives any depth walk. */
const struct _FilterXStaticTypeNode FILTERX_STATIC_TYPE_UNKNOWN_NODE =
{
  .kind    = FILTERX_STATIC_TYPE_UNKNOWN,
  .element = &FILTERX_STATIC_TYPE_UNKNOWN_NODE,
};

/* --- Frozen type-chain pool ----------------------------------------------------------
 *
 * FilterXStaticTypeSpec is a pointer to an immutable, hash-consed chain node. The pool below
 * is the single owner of every node; it is process-global and never torn down. The set of
 * distinct chains a config produces is tiny and deduplicated here (and across reloads), so a
 * spec cached on an expression stays valid for the whole life of the expression. Freezing
 * is only exercised during config init (not a hot path) and is guarded by a recursive mutex;
 * reads of an already-frozen node (kind/element) are lock-free, as nodes are immutable. */
static GRecMutex type_pool_lock;
static GHashTable *type_pool; /* set of struct _FilterXStaticTypeNode*, keyed by (kind, element) */

static guint
_node_hash(gconstpointer p)
{
  const struct _FilterXStaticTypeNode *n = p;
  return ((guint) n->kind) ^ (g_direct_hash(n->element) * 2654435761u);
}

static gboolean
_node_equal(gconstpointer a, gconstpointer b)
{
  const struct _FilterXStaticTypeNode *na = a, *nb = b;
  return na->kind == nb->kind && na->element == nb->element;
}

/* Return the canonical node for {kind, element}, allocating it on first sight. UNKNOWN maps
 * to the canonical unknown singleton. @element must already be frozen (and is never NULL). */
static FilterXStaticTypeSpec
_get_or_create_frozen_type_spec(FilterXStaticType kind, FilterXStaticTypeSpec element)
{
  if (kind == FILTERX_STATIC_TYPE_UNKNOWN)
    return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;

  g_rec_mutex_lock(&type_pool_lock);

  if (!type_pool)
    type_pool = g_hash_table_new(_node_hash, _node_equal);

  struct _FilterXStaticTypeNode probe = { .kind = kind, .element = element };
  FilterXStaticTypeSpec existing = g_hash_table_lookup(type_pool, &probe);
  if (existing)
    {
      g_rec_mutex_unlock(&type_pool_lock);
      return existing;
    }

  struct _FilterXStaticTypeNode *node = g_new0(struct _FilterXStaticTypeNode, 1);
  node->kind = kind;
  node->element = element;
  g_hash_table_insert(type_pool, node, node);

  g_rec_mutex_unlock(&type_pool_lock);
  return node;
}

FilterXStaticTypeSpec
filterx_static_type_make_container(FilterXStaticType kind, FilterXStaticTypeSpec element)
{
  return _get_or_create_frozen_type_spec(kind, element);
}

FilterXStaticTypeSpec
filterx_static_type_kind_only(FilterXStaticType kind)
{
  return _get_or_create_frozen_type_spec(kind, FILTERX_STATIC_TYPE_UNKNOWN_SPEC);
}

FilterXStaticTypeSpec
filterx_static_type_spec_meet(FilterXStaticTypeSpec a, FilterXStaticTypeSpec b)
{
  /* Longest common prefix where kinds agree; diverging or UNKNOWN levels (and everything
   * below) collapse to UNKNOWN. Frozen, so the result is comparable by pointer identity. */
  if (a == b)
    return a;                                              /* covers UNKNOWN == UNKNOWN */
  if (a->kind != b->kind)
    return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;               /* UNKNOWN-vs-anything diverges here too */
  return filterx_static_type_make_container(a->kind, filterx_static_type_spec_meet(a->element, b->element));
}

struct _FilterXTypeEnv
{
  /* key: FilterXVariableHandle via GUINT_TO_POINTER; value: FilterXStaticTypeSpec (frozen
   * node pointer, borrowed from the global pool). Absence is equivalent to all-UNKNOWN, so we
   * never store the UNKNOWN singleton; reads normalize the GLib NULL-for-absent back to it. */
  GHashTable *handle_to_spec;
};

FilterXTypeEnv *
filterx_type_env_new(void)
{
  FilterXTypeEnv *self = g_new0(FilterXTypeEnv, 1);
  self->handle_to_spec = g_hash_table_new(g_direct_hash, g_direct_equal);
  return self;
}

FilterXTypeEnv *
filterx_type_env_clone(const FilterXTypeEnv *self)
{
  FilterXTypeEnv *clone = filterx_type_env_new();
  GHashTableIter iter;
  gpointer k, v;

  g_hash_table_iter_init(&iter, self->handle_to_spec);
  while (g_hash_table_iter_next(&iter, &k, &v))
    g_hash_table_insert(clone->handle_to_spec, k, v);

  return clone;
}

void
filterx_type_env_free(FilterXTypeEnv *self)
{
  if (!self)
    return;
  g_hash_table_destroy(self->handle_to_spec);
  g_free(self);
}

FilterXStaticTypeSpec
filterx_type_env_get(const FilterXTypeEnv *self, FilterXVariableHandle handle)
{
  /* Absent key (GLib NULL) is the all-UNKNOWN default; normalize it to the unknown singleton. */
  FilterXStaticTypeSpec spec = (FilterXStaticTypeSpec) g_hash_table_lookup(self->handle_to_spec,
                               GUINT_TO_POINTER(handle));
  return spec ? spec : FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
}

void
filterx_type_env_set(FilterXTypeEnv *self, FilterXVariableHandle handle, FilterXStaticTypeSpec spec)
{
  /* UNKNOWN is the absent-key default, so we never store it — drop the entry instead. */
  if (spec == FILTERX_STATIC_TYPE_UNKNOWN_SPEC)
    g_hash_table_remove(self->handle_to_spec, GUINT_TO_POINTER(handle));
  else
    g_hash_table_insert(self->handle_to_spec, GUINT_TO_POINTER(handle), (gpointer) spec);
}

void
filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src)
{
  GHashTableIter iter;
  gpointer k, v;
  GPtrArray *to_drop = g_ptr_array_new();
  GPtrArray *to_meet_keys = g_ptr_array_new();
  GPtrArray *to_meet_specs = g_ptr_array_new();

  g_hash_table_iter_init(&iter, dst->handle_to_spec);
  while (g_hash_table_iter_next(&iter, &k, &v))
    {
      gpointer sv = g_hash_table_lookup(src->handle_to_spec, k);
      FilterXStaticTypeSpec dst_spec = (FilterXStaticTypeSpec) v;
      FilterXStaticTypeSpec src_spec = sv ? (FilterXStaticTypeSpec) sv : FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
      FilterXStaticTypeSpec met = filterx_static_type_spec_meet(dst_spec, src_spec);
      if (met == FILTERX_STATIC_TYPE_UNKNOWN_SPEC)
        g_ptr_array_add(to_drop, k);
      else if (met != dst_spec)
        {
          g_ptr_array_add(to_meet_keys, k);
          g_ptr_array_add(to_meet_specs, (gpointer) met);
        }
    }

  for (guint i = 0; i < to_drop->len; i++)
    g_hash_table_remove(dst->handle_to_spec, g_ptr_array_index(to_drop, i));
  for (guint i = 0; i < to_meet_keys->len; i++)
    g_hash_table_insert(dst->handle_to_spec, g_ptr_array_index(to_meet_keys, i),
                        g_ptr_array_index(to_meet_specs, i));
  g_ptr_array_free(to_drop, TRUE);
  g_ptr_array_free(to_meet_keys, TRUE);
  g_ptr_array_free(to_meet_specs, TRUE);
}

/* Placeholder: per-key attribute chain lookup is implemented by a later change. */
FilterXStaticTypeSpec
filterx_type_env_get_attr_for_chain(const FilterXTypeEnv *self, FilterXExpr *chain_expr,
                                    const gchar *key)
{
  return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
}

/* Placeholder: per-key attribute chain recording is implemented by a later change. */
void
filterx_type_env_meet_attr_for_chain(FilterXTypeEnv *self, FilterXExpr *chain_expr,
                                     const gchar *key, FilterXStaticTypeSpec spec)
{
}

/* Placeholder: lift-on-write container refinement is implemented by a later change. */
void
filterx_type_env_update_on_write(FilterXTypeEnv *env, FilterXExpr *container_expr,
                                 FilterXStaticTypeSpec value_spec)
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

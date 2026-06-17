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
#include "filterx/expr-variable.h"
#include "filterx/expr-getattr.h"
#include "filterx/expr-get-subscript.h"

/* The canonical "no information" spec. It lives outside the pool (it has no concrete kind to
 * key on) and is the single representation of UNKNOWN: specs are never NULL. Its element and
 * sanitized point at itself, so it terminates every element chain and survives any depth walk. */
const struct _FilterXStaticTypeNode FILTERX_STATIC_TYPE_UNKNOWN_NODE =
{
  .kind      = FILTERX_STATIC_TYPE_UNKNOWN,
  .element   = &FILTERX_STATIC_TYPE_UNKNOWN_NODE,
  .sanitized = &FILTERX_STATIC_TYPE_UNKNOWN_NODE,
};

/* --- Frozen type-chain pool ----------------------------------------------------------
 *
 * FilterXStaticTypeSpec is a pointer to an immutable, hash-consed chain node. The pool below
 * is the single owner of every node; it is process-global and never torn down. The set of
 * distinct chains a config produces is tiny and is deduplicated here (and across reloads),
 * so the memory is bounded and a spec stays valid for the whole life of any FilterXExpr that
 * caches it — including at JIT-compile time, after the inference env is freed.
 *
 * The pool is guarded by a recursive mutex: freezing is only exercised during config init
 * (single-threaded in practice), and computing a node's sanitized form re-enters _get_or_create_frozen_type_spec().
 * Reads of an already-frozen node (kind/element/sanitized) are lock-free: nodes are
 * immutable once _get_or_create_frozen_type_spec() returns them. */
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

  struct _FilterXStaticTypeNode probe = { .kind = kind, .element = element, .sanitized = NULL };
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

  /* Precompute the sanitized form (chain truncated at the first FRESH level) so that
   * filterx_static_type_sanitize()/element() are plain pointer reads at consumer time. */
  if (kind == FILTERX_STATIC_TYPE_FRESH)
    node->sanitized = FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
  else if (element->sanitized == element)
    node->sanitized = node;                                /* no FRESH at/below this level (incl. UNKNOWN-terminated) */
  else
    node->sanitized = _get_or_create_frozen_type_spec(kind, element->sanitized);   /* drop the FRESH-tainted tail */

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
  /* Per-key attribute type tracking for chained variable.k0.k1...key accesses.
   * key: heap-allocated "handle:key0:key1..." string; value: FilterXStaticTypeSpec (borrowed).
   * Tracks heterogeneous-field dicts (e.g. log.observed_time → INTEGER, log.severity_text →
   * STRING) where the container element type would otherwise smear to UNKNOWN. */
  GHashTable *attr_to_spec;
};

FilterXTypeEnv *
filterx_type_env_new(void)
{
  FilterXTypeEnv *self = g_new0(FilterXTypeEnv, 1);
  self->handle_to_spec = g_hash_table_new(g_direct_hash, g_direct_equal);
  self->attr_to_spec = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
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

  g_hash_table_iter_init(&iter, self->attr_to_spec);
  while (g_hash_table_iter_next(&iter, &k, &v))
    g_hash_table_insert(clone->attr_to_spec, g_strdup((const gchar *) k), v);

  return clone;
}

void
filterx_type_env_free(FilterXTypeEnv *self)
{
  if (!self)
    return;
  g_hash_table_destroy(self->handle_to_spec);
  g_hash_table_destroy(self->attr_to_spec);
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

  /* Also meet attr-to-spec entries: keys only in dst get dropped (source treats them as UNKNOWN). */
  GPtrArray *attr_to_drop = g_ptr_array_new_with_free_func(g_free);
  GPtrArray *attr_to_meet_keys = g_ptr_array_new_with_free_func(g_free);
  GPtrArray *attr_to_meet_specs = g_ptr_array_new();

  g_hash_table_iter_init(&iter, dst->attr_to_spec);
  while (g_hash_table_iter_next(&iter, &k, &v))
    {
      gpointer sv = g_hash_table_lookup(src->attr_to_spec, k);
      FilterXStaticTypeSpec dst_spec = (FilterXStaticTypeSpec) v;
      FilterXStaticTypeSpec src_spec = sv ? (FilterXStaticTypeSpec) sv : FILTERX_STATIC_TYPE_UNKNOWN_SPEC;
      FilterXStaticTypeSpec met = filterx_static_type_spec_meet(dst_spec, src_spec);
      if (met == FILTERX_STATIC_TYPE_UNKNOWN_SPEC)
        g_ptr_array_add(attr_to_drop, g_strdup((const gchar *) k));
      else if (met != dst_spec)
        {
          g_ptr_array_add(attr_to_meet_keys, g_strdup((const gchar *) k));
          g_ptr_array_add(attr_to_meet_specs, (gpointer) met);
        }
    }

  for (guint i = 0; i < attr_to_drop->len; i++)
    g_hash_table_remove(dst->attr_to_spec, g_ptr_array_index(attr_to_drop, i));
  for (guint i = 0; i < attr_to_meet_keys->len; i++)
    g_hash_table_insert(dst->attr_to_spec,
                        g_strdup((const gchar *) g_ptr_array_index(attr_to_meet_keys, i)),
                        g_ptr_array_index(attr_to_meet_specs, i));
  g_ptr_array_free(attr_to_drop, TRUE);
  g_ptr_array_free(attr_to_meet_keys, TRUE);
  g_ptr_array_free(attr_to_meet_specs, TRUE);
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

/* Walk a getattr/get_subscript chain down to its root variable, counting the number of
 * hops. Returns FALSE if the chain does not bottom out at a trackable (non-macro) variable. */
static gboolean
_walk_to_root_variable(FilterXExpr *expr, FilterXVariableHandle *handle_out, gint *depth_out)
{
  gint depth = 0;
  while (expr)
    {
      if (filterx_variable_expr_get_handle(expr, handle_out))
        {
          *depth_out = depth;
          return TRUE;
        }
      if (filterx_expr_is_getattr(expr))
        expr = filterx_getattr_get_operand(expr);
      else if (filterx_expr_is_get_subscript(expr))
        expr = filterx_get_subscript_get_operand(expr);
      else
        return FALSE;
      depth++;
    }
  return FALSE;
}

/* Maximum getattr chain depth tracked in the attr-path map.
 * Covers chains up to variable.k0.k1.k2.k3 (4 attribute hops from the root). */
#define FILTERX_ATTR_CHAIN_MAX 4

/* Walk a purely-getattr chain to its root variable, collecting attribute names
 * top-down into keys_out[0..n_keys_out-1].  Returns FALSE for subscript hops,
 * macro variables, or chains deeper than FILTERX_ATTR_CHAIN_MAX. */
static gboolean
_collect_attr_chain(FilterXExpr *expr, FilterXVariableHandle *handle_out,
                    const gchar **keys_out, gint *n_keys_out)
{
  gint n = 0;
  while (expr)
    {
      if (filterx_variable_expr_get_handle(expr, handle_out))
        {
          /* Keys were appended bottom-up; reverse to top-down. */
          for (gint i = 0, j = n - 1; i < j; i++, j--)
            {
              const gchar *tmp = keys_out[i];
              keys_out[i] = keys_out[j];
              keys_out[j] = tmp;
            }
          *n_keys_out = n;
          return TRUE;
        }
      if (!filterx_expr_is_getattr(expr) || n >= FILTERX_ATTR_CHAIN_MAX)
        return FALSE;
      keys_out[n++] = expr->name;   /* FilterXExpr::name holds the attr key for getattr nodes */
      expr = filterx_getattr_get_operand(expr);
    }
  return FALSE;
}

/* Build the hash-table key string for a chained path.
 * Format: "handle:key0:key1:...:keyN"  (n_keys entries after the handle). */
static gchar *
_attr_chain_key(FilterXVariableHandle handle, const gchar **keys, gint n_keys)
{
  GString *s = g_string_sized_new(64);
  g_string_append_printf(s, "%u", (guint) handle);
  for (gint i = 0; i < n_keys; i++)
    {
      g_string_append_c(s, ':');
      g_string_append(s, keys[i]);
    }
  return g_string_free(s, FALSE);
}

/* Look up the type of <chain_expr>.<key> where chain_expr is a purely-getattr
 * chain rooted at a non-macro variable.  Returns UNKNOWN if the path is not
 * recorded or the chain is too deep. */
FilterXStaticTypeSpec
filterx_type_env_get_attr_for_chain(const FilterXTypeEnv *self, FilterXExpr *chain_expr,
                                    const gchar *key)
{
  const gchar *keys[FILTERX_ATTR_CHAIN_MAX + 1];
  gint n = 0;
  FilterXVariableHandle handle;

  if (!_collect_attr_chain(chain_expr, &handle, keys, &n) || n + 1 > FILTERX_ATTR_CHAIN_MAX + 1)
    return FILTERX_STATIC_TYPE_UNKNOWN_SPEC;

  keys[n] = key;
  gchar *k = _attr_chain_key(handle, keys, n + 1);
  FilterXStaticTypeSpec v = (FilterXStaticTypeSpec) g_hash_table_lookup(self->attr_to_spec, k);
  g_free(k);
  /* Absent key (GLib NULL) is all-UNKNOWN; normalize before the (non-NULL) sanitize read. */
  return filterx_static_type_sanitize(v ? v : FILTERX_STATIC_TYPE_UNKNOWN_SPEC);
}

/* Record that <chain_expr>.<key> = spec (using meet semantics for updates). */
void
filterx_type_env_meet_attr_for_chain(FilterXTypeEnv *self, FilterXExpr *chain_expr,
                                     const gchar *key, FilterXStaticTypeSpec spec)
{
  if (spec == FILTERX_STATIC_TYPE_UNKNOWN_SPEC)
    return;

  const gchar *keys[FILTERX_ATTR_CHAIN_MAX + 1];
  gint n = 0;
  FilterXVariableHandle handle;

  if (!_collect_attr_chain(chain_expr, &handle, keys, &n) || n + 1 > FILTERX_ATTR_CHAIN_MAX + 1)
    return;

  keys[n] = key;
  gchar *k = _attr_chain_key(handle, keys, n + 1);
  FilterXStaticTypeSpec old = (FilterXStaticTypeSpec) g_hash_table_lookup(self->attr_to_spec, k);
  FilterXStaticTypeSpec merged = (!old) ? spec : filterx_static_type_spec_meet(old, spec);
  if (merged == FILTERX_STATIC_TYPE_UNKNOWN_SPEC)
    {
      g_hash_table_remove(self->attr_to_spec, k);
      g_free(k);
    }
  else if (merged != old)
    g_hash_table_insert(self->attr_to_spec, k, (gpointer) merged);
  else
    g_free(k);
}

/* Follow the element chain @depth levels down. Walking past the end stays on the UNKNOWN
 * singleton (its element is itself), so a too-short chain yields UNKNOWN. */
static FilterXStaticTypeSpec
_node_at_depth(FilterXStaticTypeSpec spec, gint depth)
{
  for (gint i = 0; i < depth; i++)
    spec = spec->element;
  return spec;
}

/* Rebuild @spec with the subchain at @depth replaced by @newtail, re-freezing the kept
 * prefix. @spec is guaranteed non-NULL down to @depth by the caller. */
static FilterXStaticTypeSpec
_set_at_depth(FilterXStaticTypeSpec spec, gint depth, FilterXStaticTypeSpec newtail)
{
  if (depth == 0)
    return newtail;
  return filterx_static_type_make_container(spec->kind,
                                            _set_at_depth(spec->element, depth - 1, newtail));
}

void
filterx_type_env_update_on_write(FilterXTypeEnv *env, FilterXExpr *container_expr,
                                 FilterXStaticTypeSpec value_spec)
{
  FilterXVariableHandle handle;
  gint depth;
  if (!_walk_to_root_variable(container_expr, &handle, &depth))
    return;

  /* The written value lands one level below the container we wrote into. */
  gint level = depth + 1;

  FilterXStaticTypeSpec old = filterx_type_env_get(env, handle);
  FilterXStaticTypeSpec suffix = _node_at_depth(old, level);
  if (suffix == FILTERX_STATIC_TYPE_UNKNOWN_SPEC)
    return;   /* UNKNOWN at the written level: no committed type to refine from */

  FilterXStaticTypeSpec newtail;
  if (suffix->kind == FILTERX_STATIC_TYPE_FRESH)
    newtail = value_spec;                                         /* lift: commit to the written type */
  else
    newtail = filterx_static_type_spec_meet(suffix, value_spec);  /* meet with the committed type */

  filterx_type_env_set(env, handle, _set_at_depth(old, level, newtail));
}

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
#include "filterx/filterx-type-inference-private.h"
#include "filterx/filterx-expr.h"
#include "filterx/expr-variable.h"
#include "filterx/expr-getattr.h"
#include "filterx/expr-get-subscript.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"

#include <string.h>

/* The fact is packed into the value pointer, so there is nothing to allocate or free on the value
 * side.  PRESENT keeps a valid fact from ever packing to NULL, which g_tree_lookup() already
 * spends on "no entry" -- {UNKNOWN, not closed} would otherwise be indistinguishable from a miss,
 * and a closed container holding one key of an unknown type must not read as empty. */
#define FX_FACT_KIND_MASK 0x0fu
#define FX_FACT_CLOSED    0x10u
#define FX_FACT_PRESENT   0x20u

static inline gpointer
_fact_pack(FilterXStaticType kind, gboolean closed)
{
  guint packed = FX_FACT_PRESENT | ((guint) kind & FX_FACT_KIND_MASK) | (closed ? FX_FACT_CLOSED : 0);
  return GUINT_TO_POINTER(packed);
}

static inline FilterXStaticType
_fact_kind(gpointer fact)
{
  return (FilterXStaticType) (GPOINTER_TO_UINT(fact) & FX_FACT_KIND_MASK);
}

static inline gboolean
_fact_is_closed(gpointer fact)
{
  return (GPOINTER_TO_UINT(fact) & FX_FACT_CLOSED) != 0;
}

struct _FilterXTypeEnv
{
  GTree *facts;   /* FilterXTypePath * (owned) -> a packed fact */
};

/* --- the lattice ---------------------------------------------------------------------------- */

static inline gboolean
_is_numeric_static_type(FilterXStaticType kind)
{
  return kind == FILTERX_STATIC_TYPE_INTEGER || kind == FILTERX_STATIC_TYPE_DOUBLE;
}

FilterXStaticType
filterx_static_type_numeric_add_result(FilterXStaticType a, FilterXStaticType b)
{
  if (!_is_numeric_static_type(a) || !_is_numeric_static_type(b))
    return FILTERX_STATIC_TYPE_UNKNOWN;

  gboolean widens = (a == FILTERX_STATIC_TYPE_DOUBLE || b == FILTERX_STATIC_TYPE_DOUBLE);
  return widens ? FILTERX_STATIC_TYPE_DOUBLE : FILTERX_STATIC_TYPE_INTEGER;
}

/* --- the tree ------------------------------------------------------------------------------- */

FilterXTypeEnv *
filterx_type_env_new(void)
{
  FilterXTypeEnv *self = g_new0(FilterXTypeEnv, 1);
  self->facts = g_tree_new_full(filterx_type_path_compare, NULL, g_free, NULL);
  return self;
}

void
filterx_type_env_free(FilterXTypeEnv *self)
{
  if (!self)
    return;
  g_tree_destroy(self->facts);
  g_free(self);
}

static inline gpointer
_lookup(const FilterXTypeEnv *self, const FilterXTypePath *path)
{
  return g_tree_lookup(self->facts, path);
}

static inline void
_insert(FilterXTypeEnv *self, const FilterXTypePath *path, gpointer fact)
{
  g_tree_insert(self->facts, filterx_type_path_dup(path), fact);
}

gboolean
filterx_type_env_lookup(const FilterXTypeEnv *self, const FilterXTypePath *path,
                        FilterXStaticType *kind_out, gboolean *closed_out)
{
  gpointer fact = _lookup(self, path);
  if (!fact)
    return FALSE;

  if (kind_out)
    *kind_out = _fact_kind(fact);
  if (closed_out)
    *closed_out = _fact_is_closed(fact);
  return TRUE;
}

/* Every subtree walk has this shape: skip forward to @root, take while @root is a prefix, stop at
 * the first entry past it.  The comparator puts a path immediately before exactly its own
 * descendants, so the run is contiguous and the early exit is exact.  d.sub and d.subscription
 * are separate steps rather than a shared character prefix, so they can never nest.
 *
 * GLib's minimum here is 2.32, so g_tree_lower_bound()/g_tree_node_next() (2.68) are out and the
 * skip stays linear.  The env holds tens of entries and lives for one pipe init. */
typedef gboolean (*FilterXTypeRangeFunc)(const FilterXTypePath *path, gpointer fact, gpointer user_data);

typedef struct
{
  const FilterXTypePath *root;
  FilterXTypeRangeFunc func;
  gpointer user_data;
} _RangeCtx;

static gboolean
_range_traverse(gpointer key, gpointer value, gpointer user_data)
{
  _RangeCtx *ctx = (_RangeCtx *) user_data;
  const FilterXTypePath *path = (const FilterXTypePath *) key;

  if (filterx_type_path_compare(path, ctx->root, NULL) < 0)
    return FALSE;                                        /* not there yet */

  if (!filterx_type_path_is_prefix_of(ctx->root, path))
    return TRUE;                                         /* past the subtree, stop */

  return ctx->func(path, value, ctx->user_data);
}

static void
_range_foreach(const FilterXTypeEnv *self, const FilterXTypePath *root,
               FilterXTypeRangeFunc func, gpointer user_data)
{
  _RangeCtx ctx = { .root = root, .func = func, .user_data = user_data };
  g_tree_foreach(self->facts, _range_traverse, &ctx);
}

/* A snapshot of a range: owned path copies plus their facts.  Mutating a GTree mid-traversal is
 * not allowed, and d.sub = d reads a range that overlaps the one it is about to drop, so every
 * removal and every copy goes through one of these. */
typedef struct
{
  GPtrArray *paths;    /* owned FilterXTypePath * */
  GArray *facts;       /* the packed facts, index-aligned with @paths */
  guint depth_filter;  /* when non-zero, only paths of exactly this many steps are collected */
} _Snapshot;

static gboolean
_snapshot_collect(const FilterXTypePath *path, gpointer fact, gpointer user_data)
{
  _Snapshot *snap = (_Snapshot *) user_data;

  if (snap->depth_filter && path->n_steps != snap->depth_filter)
    return FALSE;

  g_ptr_array_add(snap->paths, filterx_type_path_dup(path));
  g_array_append_val(snap->facts, fact);
  return FALSE;
}

static void
_snapshot_init(_Snapshot *snap)
{
  snap->paths = g_ptr_array_new_with_free_func(g_free);
  snap->facts = g_array_new(FALSE, FALSE, sizeof(gpointer));
  snap->depth_filter = 0;
}

static void
_snapshot_clear(_Snapshot *snap)
{
  g_ptr_array_free(snap->paths, TRUE);
  g_array_free(snap->facts, TRUE);
}

static void
_snapshot_range(const FilterXTypeEnv *self, const FilterXTypePath *root, _Snapshot *snap)
{
  _snapshot_init(snap);
  _range_foreach(self, root, _snapshot_collect, snap);
}

static void
_snapshot_direct_children(const FilterXTypeEnv *self, const FilterXTypePath *parent, _Snapshot *snap)
{
  _snapshot_init(snap);
  snap->depth_filter = parent->n_steps + 1;
  _range_foreach(self, parent, _snapshot_collect, snap);
}

static inline const FilterXTypePath *
_snapshot_path(const _Snapshot *snap, guint i)
{
  return (const FilterXTypePath *) g_ptr_array_index(snap->paths, i);
}

static inline gpointer
_snapshot_fact(const _Snapshot *snap, guint i)
{
  return g_array_index(snap->facts, gpointer, i);
}

static void
_drop_range(FilterXTypeEnv *self, const FilterXTypePath *root, gboolean include_root)
{
  _Snapshot snap;

  _snapshot_range(self, root, &snap);
  for (guint i = 0; i < snap.paths->len; i++)
    {
      const FilterXTypePath *path = _snapshot_path(&snap, i);
      if (!include_root && filterx_type_path_compare(path, root, NULL) == 0)
        continue;
      g_tree_remove(self->facts, path);
    }
  _snapshot_clear(&snap);
}

static void
_clear_closed(FilterXTypeEnv *self, const FilterXTypePath *path)
{
  gpointer fact = _lookup(self, path);
  if (fact && _fact_is_closed(fact))
    _insert(self, path, _fact_pack(_fact_kind(fact), FALSE));
}

/* --- reading -------------------------------------------------------------------------------- */

FilterXStaticType
filterx_type_env_get_at_path(const FilterXTypeEnv *self, const FilterXTypePath *path)
{
  /* No exact address, so no entry speaks for it. */
  if (path->truncated)
    return FILTERX_STATIC_TYPE_UNKNOWN;

  gpointer fact = _lookup(self, path);
  return fact ? _fact_kind(fact) : FILTERX_STATIC_TYPE_UNKNOWN;
}

FilterXStaticType
filterx_type_env_get_for_expr(const FilterXTypeEnv *self, FilterXExpr *expr)
{
  FilterXTypePath path;

  if (!filterx_expr_get_path(expr, &path))
    return FILTERX_STATIC_TYPE_UNKNOWN;

  return filterx_type_env_get_at_path(self, &path);
}

/* --- writing -------------------------------------------------------------------------------- */

void
filterx_type_env_set_at_path(FilterXTypeEnv *self, const FilterXTypePath *path,
                             FilterXStaticType kind, gboolean closed)
{
  if (path->truncated)
    return;

  /* Ancestors are untouched: adding a child leaves a closed parent closed, because the new key is
   * itself a recorded child now.  Nothing below survives, and nothing below can pick up a stale
   * claim either -- with the range gone, every read under @path answers UNKNOWN. */
  _drop_range(self, path, TRUE);
  _insert(self, path, _fact_pack(kind, closed));
}

void
filterx_type_env_open_at_path(FilterXTypeEnv *self, const FilterXTypePath *path)
{
  if (path->truncated)
    return;

  gpointer fact = _lookup(self, path);
  _drop_range(self, path, FALSE);

  if (fact)
    _insert(self, path, _fact_pack(_fact_kind(fact), FALSE));
}

/* --- shapes --------------------------------------------------------------------------------- */

static FilterXStaticType
_kind_from_object(FilterXObject *obj)
{
  if (!obj)
    return FILTERX_STATIC_TYPE_UNKNOWN;

  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(string)))
    return FILTERX_STATIC_TYPE_STRING;
  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(integer)))
    return FILTERX_STATIC_TYPE_INTEGER;
  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(double)))
    return FILTERX_STATIC_TYPE_DOUBLE;
  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(boolean)))
    return FILTERX_STATIC_TYPE_BOOLEAN;
  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(dict)))
    return FILTERX_STATIC_TYPE_DICT;
  if (filterx_object_is_type_or_ref(obj, &FILTERX_TYPE_NAME(list)))
    return FILTERX_STATIC_TYPE_LIST;

  return FILTERX_STATIC_TYPE_UNKNOWN;
}

typedef struct
{
  FilterXTypeEnv *env;
  const FilterXTypePath *base;
  /* FALSE once an element turned up that no path can address, which is a key of the container
   * that is not among its recorded children. */
  gboolean complete;
} _ObjectShapeCtx;

static void _create_shape(FilterXTypeEnv *env, const FilterXTypePath *base, FilterXObject *obj);

static gboolean
_install_object_element(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  _ObjectShapeCtx *ctx = (_ObjectShapeCtx *) user_data;

  /* A dict yields string keys, a list yields its indices; only the former names a step.  That is
   * also why nothing inside a list is ever recorded: filterx_sequence_normalize_index() resolves
   * an index against the runtime length, so l[-1] and l[0] can name the same slot. */
  const gchar *step = NULL;
  if (key && filterx_object_is_type_or_ref(key, &FILTERX_TYPE_NAME(string)))
    step = filterx_type_path_intern_key(filterx_string_get_value_ref_and_assert_nul(key, NULL));

  FilterXTypePath child = *ctx->base;
  if (!filterx_type_path_append_step(&child, step))
    {
      ctx->complete = FALSE;
      return TRUE;
    }

  _create_shape(ctx->env, &child, value);
  return TRUE;
}

static void
_create_shape(FilterXTypeEnv *env, const FilterXTypePath *base, FilterXObject *obj)
{
  FilterXStaticType kind = _kind_from_object(obj);

  if (kind != FILTERX_STATIC_TYPE_DICT && kind != FILTERX_STATIC_TYPE_LIST)
    {
      filterx_type_env_set_at_path(env, base, kind, FALSE);
      return;
    }

  /* An object in hand has an exactly known key set, so the container starts out closed.  An empty
   * one is closed with no children at all, which is what the old FRESH sentinel used to encode.
   *
   * It only stays closed if every key made it into a child path.  A list keeps none of its
   * elements and a dict loses the ones past the depth cap, and closed-with-no-children says the
   * container is empty -- a strictly stronger claim than the one that just went unrecorded. */
  filterx_type_env_set_at_path(env, base, kind, TRUE);

  _ObjectShapeCtx ctx = { .env = env, .base = base, .complete = TRUE };
  filterx_object_iter(obj, _install_object_element, &ctx);

  if (!ctx.complete)
    _clear_closed(env, base);
}

/* Copy range(@src) out of @src_env and re-root it at @dst in @dst_env.  The two may be the same
 * env and the two ranges may overlap -- d.sub = d and d = d.sub are both real statements -- which
 * is why the source is snapshotted before the destination is dropped. */
static void
_copy_shape(FilterXTypeEnv *dst_env, const FilterXTypePath *dst,
            const FilterXTypeEnv *src_env, const FilterXTypePath *src)
{
  _Snapshot snap;

  _snapshot_range(src_env, src, &snap);
  _drop_range(dst_env, dst, TRUE);

  /* A write makes the destination exist even when the source range is empty, and the entry has
   * to say so: without it a closed parent would read the key just written as proven absent, and
   * a join against a branch that did not write would keep that branch's stale type. */
  _insert(dst_env, dst, _fact_pack(FILTERX_STATIC_TYPE_UNKNOWN, FALSE));

  for (guint i = 0; i < snap.paths->len; i++)
    {
      const FilterXTypePath *path = _snapshot_path(&snap, i);
      gpointer fact = _snapshot_fact(&snap, i);

      FilterXTypePath rerooted = *dst;
      rerooted.truncated = FALSE;
      gboolean fits = TRUE;
      for (guint step = src->n_steps; step < path->n_steps && fits; step++)
        fits = filterx_type_path_append_step(&rerooted, path->steps[step]);

      if (!fits)
        {
          /* The copy ran out of depth, so the destination gains a key it has no entry for and the
           * deepest representable ancestor cannot go on claiming a complete key set. */
          rerooted.truncated = FALSE;
          _clear_closed(dst_env, &rerooted);
          continue;
        }

      _insert(dst_env, &rerooted, fact);
    }

  _snapshot_clear(&snap);
}

/* How much of a written value's shape this pass can see.  There are only three answers: the value
 * itself is in hand, it is a read of a location already tracked, or neither. */
typedef enum
{
  SHAPE_OPAQUE,      /* only the kind is known */
  SHAPE_OBSERVABLE,  /* a folded literal, or a partially evaluated literal container */
  SHAPE_OBSERVED,    /* a read of already tracked path in this env */
} _ShapeKind;

typedef struct
{
  _ShapeKind kind;
  FilterXStaticType expr_type;
  FilterXObject *object;          /* SHAPE_OBSERVABLE, owned when @object_owned */
  gboolean object_owned;
  FilterXTypePath src_path;       /* SHAPE_OBSERVED */
} _Shape;

static _Shape
_shape_of(FilterXExpr *rhs_expr)
{
  _Shape shape = { .kind = SHAPE_OPAQUE, .expr_type = FILTERX_STATIC_TYPE_UNKNOWN };

  if (!rhs_expr)
    return shape;

  shape.expr_type = rhs_expr->static_type;

  if (filterx_expr_is_literal(rhs_expr))
    {
      shape.kind = SHAPE_OBSERVABLE;
      shape.object = filterx_literal_get_value(rhs_expr);
      shape.object_owned = TRUE;
      return shape;
    }

  if (filterx_expr_get_path(rhs_expr, &shape.src_path) && !shape.src_path.truncated)
    shape.kind = SHAPE_OBSERVED;

  return shape;
}

static void
_clear_shape(_Shape *shape)
{
  if (shape->object_owned)
    filterx_object_unref(shape->object);
}

static void
_install_shape(FilterXTypeEnv *self, const FilterXTypePath *path, _Shape *shape)
{
  switch (shape->kind)
    {
    case SHAPE_OBSERVABLE:
      _create_shape(self, path, shape->object);
      break;
    case SHAPE_OBSERVED:
      _copy_shape(self, path, self, &shape->src_path);
      break;
    case SHAPE_OPAQUE:
    default:
      filterx_type_env_set_at_path(self, path, shape->expr_type, FALSE);
      break;
    }
}

void
filterx_type_env_set_shape_at_path(FilterXTypeEnv *self, const FilterXTypePath *path, FilterXExpr *rhs_expr)
{
  if (path->truncated)
    {
      /* The write has no exact address -- a key nothing can name, or a level past the depth cap --
       * so it may have landed anywhere under the deepest addressable ancestor, and that ancestor
       * is all this env can still speak about.  `d = {}; d[$k] = "s";` leaves d a DICT with no
       * leaves, which is what makes the `d.a = 1` after it the only thing d.a can be. */
      FilterXTypePath prefix = *path;
      prefix.truncated = FALSE;
      filterx_type_env_open_at_path(self, &prefix);
      return;
    }

  _Shape shape = _shape_of(rhs_expr);

  _install_shape(self, path, &shape);

  _clear_shape(&shape);
}

void
filterx_type_env_clear_at_path(FilterXTypeEnv *self, const FilterXTypePath *path)
{
  if (path->truncated)
    {
      /* The victim cannot be named.  A removal falsifies nothing on its own -- it only makes reads
       * answer C NULL, and only shrinks a key set `closed` bounds from above -- but opening the
       * deepest addressable ancestor costs nothing and leaves no lemma to re-derive. */
      FilterXTypePath prefix = *path;
      prefix.truncated = FALSE;
      filterx_type_env_open_at_path(self, &prefix);
      return;
    }

  /* unset() on a named key proves the key gone, so the parent's `closed` survives with a smaller
   * key set and there is no obligation to discharge. */
  _drop_range(self, path, TRUE);
}

/* --- the expression-level entry points ------------------------------------------------------ */

void
filterx_type_env_update_on_remove(FilterXTypeEnv *self, FilterXExpr *target_expr)
{
  FilterXTypePath path;

  if (!target_expr || !filterx_expr_get_path(target_expr, &path))
    return;

  filterx_type_env_clear_at_path(self, &path);
}

FilterXStaticType
filterx_type_env_update_on_inplace_modify(FilterXTypeEnv *self, FilterXExpr *target_expr, FilterXExpr *rhs_expr)
{
  FilterXTypePath path;

  if (!target_expr || !filterx_expr_get_path(target_expr, &path))
    return FILTERX_STATIC_TYPE_UNKNOWN;

  if (path.truncated)
    {
      /* The read answers UNKNOWN and the write would land at a path this env cannot address, so
       * retire what is below the representable prefix instead of leaving a stale claim. */
      FilterXTypePath prefix = path;
      prefix.truncated = FALSE;
      filterx_type_env_open_at_path(self, &prefix);
      return FILTERX_STATIC_TYPE_UNKNOWN;
    }

  FilterXStaticType target = filterx_type_env_get_at_path(self, &path);
  FilterXStaticType rhs = rhs_expr ? rhs_expr->static_type : FILTERX_STATIC_TYPE_UNKNOWN;

  /* What `target += rhs` leaves in target: numeric promotion, or a same-kind merge/concatenation. */
  FilterXStaticType merged = filterx_static_type_numeric_add_result(target, rhs);
  if (merged == FILTERX_STATIC_TYPE_UNKNOWN &&
      target == rhs &&
      (target == FILTERX_STATIC_TYPE_DICT ||
       target == FILTERX_STATIC_TYPE_LIST ||
       target == FILTERX_STATIC_TYPE_STRING))
    merged = target;

  if (merged != FILTERX_STATIC_TYPE_DICT)
    {
      filterx_type_env_set_at_path(self, &path, merged, FALSE);
      return merged;
    }

  /* `target += rhs` on a dict is filterx_mapping_merge(): a shallow per-key overwrite, so the
   * RHS's keys win and the target's own keys survive. */
  gpointer target_fact = _lookup(self, &path);
  if (!target_fact)
    return merged;

  _Shape shape = _shape_of(rhs_expr);

  /* Only a dict merge is per-key; a list or string concatenation says nothing about which of the
   * target's own keys survived where. */
  if (_fact_kind(target_fact) != FILTERX_STATIC_TYPE_DICT || shape.expr_type != FILTERX_STATIC_TYPE_DICT)
    {
      filterx_type_env_open_at_path(self, &path);
      _clear_shape(&shape);
      return merged;
    }

  /* Materialise the RHS under a scratch root first: it may be a range of this very env, and the
   * per-key overwrite below would otherwise read entries it has already replaced. */
  FilterXTypeEnv *scratch = filterx_type_env_new();
  FilterXTypePath scratch_root = { .root = 0, .n_steps = 0 };

  if (shape.kind == SHAPE_OBSERVED)
    _copy_shape(scratch, &scratch_root, self, &shape.src_path);
  else
    _install_shape(scratch, &scratch_root, &shape);

  gpointer rhs_fact = _lookup(scratch, &scratch_root);
  gboolean rhs_closed = rhs_fact && _fact_is_closed(rhs_fact);

  /* Only a closed RHS has a key set this pass can enumerate.  An open one may carry a key it never
   * recorded, and that key overwrites the target's own with a value nothing here has seen -- so
   * nothing below the target survives, though the merge itself still leaves a dict. */
  if (!rhs_closed)
    {
      filterx_type_env_open_at_path(self, &path);
      filterx_type_env_free(scratch);
      _clear_shape(&shape);
      return merged;
    }

  gboolean target_closed = _fact_is_closed(target_fact);

  _Snapshot children;
  _snapshot_direct_children(scratch, &scratch_root, &children);

  for (guint i = 0; i < children.paths->len; i++)
    {
      const FilterXTypePath *child = _snapshot_path(&children, i);
      const gchar *step = child->steps[child->n_steps - 1];

      FilterXTypePath dst = path;
      if (!filterx_type_path_append_step(&dst, step))
        {
          /* The key the merge brings in lands past the depth cap, so the target gains one it has
           * no entry for. */
          _clear_closed(self, &path);
          continue;
        }
      dst.truncated = FALSE;

      /* filterx_mapping_merge() is a shallow per-key overwrite, so a key of the RHS simply
       * replaces the target's. */
      _copy_shape(self, &dst, scratch, child);
    }
  _snapshot_clear(&children);

  /* The RHS is closed, so its keys are all accounted for above and only the target's own key set
   * decides whether the result is still bounded. */
  _insert(self, &path, _fact_pack(FILTERX_STATIC_TYPE_DICT, target_closed));

  filterx_type_env_free(scratch);
  _clear_shape(&shape);

  return merged;
}

static gboolean
_invalidate_argument(FilterXExpr *parent, FilterXExpr **child, gpointer user_data)
{
  FilterXTypeEnv *env = (FilterXTypeEnv *) user_data;
  FilterXTypePath path;

  if (*child && filterx_expr_get_path(*child, &path))
    {
      path.truncated = FALSE;   /* the representable prefix is what there is to open */
      filterx_type_env_open_at_path(env, &path);
    }

  return TRUE;
}

void
filterx_type_env_invalidate_arguments(FilterXTypeEnv *self, FilterXExpr *expr)
{
  filterx_expr_walk_children(expr, _invalidate_argument, self);
}

/* --- the join ------------------------------------------------------------------------------- */

static gboolean
_collect_every(gpointer key, gpointer value, gpointer user_data)
{
  _Snapshot *snap = (_Snapshot *) user_data;

  g_ptr_array_add(snap->paths, filterx_type_path_dup((const FilterXTypePath *) key));
  g_array_append_val(snap->facts, value);
  return FALSE;
}

static void
_snapshot_all(const FilterXTypeEnv *self, _Snapshot *snap)
{
  _snapshot_init(snap);
  g_tree_foreach(self->facts, _collect_every, snap);
}

/* The deepest strict ancestor of @path that @dst has an entry for. */
static gboolean
_deepest_recorded_ancestor(const FilterXTypeEnv *dst, const FilterXTypePath *path, FilterXTypePath *out)
{
  FilterXTypePath probe = *path;

  while (probe.n_steps > 0)
    {
      probe.n_steps--;
      if (_lookup(dst, &probe))
        {
          *out = probe;
          return TRUE;
        }
    }
  return FALSE;
}

void
filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src)
{
  _Snapshot snap;

  /* Pass 1 -- every dst entry against the same path in src. */
  _snapshot_all(dst, &snap);

  for (guint i = 0; i < snap.paths->len; i++)
    {
      const FilterXTypePath *path = _snapshot_path(&snap, i);
      gpointer dst_fact = _snapshot_fact(&snap, i);
      gpointer src_fact = _lookup(src, path);

      if (src_fact)
        {
          /* Both sides claim the value exists, so the entry stays even when the kinds disagree;
           * dropping it would report a key of a closed container as absent. */
          _insert(dst, path, _fact_pack(filterx_static_type_meet(_fact_kind(dst_fact), _fact_kind(src_fact)),
                                        _fact_is_closed(dst_fact) && _fact_is_closed(src_fact)));
          continue;
        }

      if (path->n_steps == 0)
        {
          /* A variable missing from src is one src knows nothing about, not one it proved unset. */
          g_tree_remove(dst->facts, path);
          continue;
        }

      FilterXTypePath parent;
      filterx_type_path_parent(path, &parent);

      gpointer src_parent = _lookup(src, &parent);
      if (src_parent && _fact_is_closed(src_parent))
        {
          /* src proves the key absent, and an absent key reads as NULL, so src constrains nothing
           * and the entry survives the join intact.  This is what keeps d.a INTEGER after
           * `d = {}; if (c) { d.a = 1; }`. */
          continue;
        }

      /* src may hold this key at a type nothing here has seen, so the entry goes -- and with it
       * the parent's claim to a complete key set, which counted the key just dropped. */
      g_tree_remove(dst->facts, path);
      _clear_closed(dst, &parent);
    }

  _snapshot_clear(&snap);

  /* Pass 2 -- a key src knows about and dst does not breaks dst's claim to a complete key set. */
  _snapshot_all(src, &snap);

  for (guint i = 0; i < snap.paths->len; i++)
    {
      const FilterXTypePath *path = _snapshot_path(&snap, i);

      if (_lookup(dst, path))
        continue;

      FilterXTypePath ancestor;
      if (_deepest_recorded_ancestor(dst, path, &ancestor))
        _clear_closed(dst, &ancestor);
    }

  _snapshot_clear(&snap);
}

/* --- cloning -------------------------------------------------------------------------------- */

typedef struct
{
  FilterXTypeEnv *clone;
  FilterXTypeEnvHandlePredicate pred;
  gpointer user_data;
} _CloneCtx;

static gboolean
_clone_entry(gpointer key, gpointer value, gpointer user_data)
{
  _CloneCtx *ctx = (_CloneCtx *) user_data;
  const FilterXTypePath *path = (const FilterXTypePath *) key;

  if (!ctx->pred || ctx->pred(path->root, ctx->user_data))
    g_tree_insert(ctx->clone->facts, filterx_type_path_dup(path), value);
  return FALSE;
}

FilterXTypeEnv *
filterx_type_env_clone(const FilterXTypeEnv *self)
{
  _CloneCtx ctx = { .clone = filterx_type_env_new() };

  g_tree_foreach(self->facts, _clone_entry, &ctx);
  return ctx.clone;
}

FilterXTypeEnv *
filterx_type_env_clone_filtered(const FilterXTypeEnv *self, FilterXTypeEnvHandlePredicate pred, gpointer user_data)
{
  _CloneCtx ctx = { .clone = filterx_type_env_new(), .pred = pred, .user_data = user_data };

  g_tree_foreach(self->facts, _clone_entry, &ctx);
  return ctx.clone;
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

  self->types_inferred = TRUE;
#endif
}

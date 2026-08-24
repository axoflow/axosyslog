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
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/filterx-dpath.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "logmsg/logmsg.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* A fact is packed into the GTree value pointer, so there is nothing to own on the value side.
 * PRESENT keeps {UNKNOWN, not closed} from packing to NULL, which g_tree_lookup() already spends
 * on "no entry". */
#define FX_FACT_STATIC_TYPE_MASK 0x0fu
#define FX_FACT_CLOSED           0x10u
#define FX_FACT_PRESENT          0x20u

static inline gpointer
_fact_pack(FilterXStaticType static_type, gboolean closed)
{
  guint packed = FX_FACT_PRESENT | ((guint) static_type & FX_FACT_STATIC_TYPE_MASK) | (closed ? FX_FACT_CLOSED : 0);
  return GUINT_TO_POINTER(packed);
}

static inline FilterXStaticType
_fact_static_type(gpointer fact)
{
  return (FilterXStaticType) (GPOINTER_TO_UINT(fact) & FX_FACT_STATIC_TYPE_MASK);
}

static inline gboolean
_fact_is_closed(gpointer fact)
{
  return (GPOINTER_TO_UINT(fact) & FX_FACT_CLOSED) != 0;
}

struct _FilterXTypeEnv
{
  GTree *facts;   /* FilterXAccessPath * (owned) -> a packed fact */
};

/* --- the trace ------------------------------------------------------------------------------ */

/* the pass runs once per block on the config-reading thread, so this needs no locking */
static gint _trace_depth = 0;

gboolean
filterx_type_inference_trace_enabled(void)
{
  static gint enabled = -1;

  if (enabled < 0)
    enabled = g_getenv("SYSLOG_NG_FILTERX_TRACE_TYPES") ? 1 : 0;
  return enabled == 1;
}

static void G_GNUC_PRINTF(1, 2)
_trace_printf(const gchar *format, ...)
{
  va_list ap;

  fprintf(stderr, "%*s", _trace_depth * 2, "");
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fputc('\n', stderr);
}

/* a macro so that a disabled trace also skips formatting the arguments */
#define _trace(...) \
  do { \
    if (filterx_type_inference_trace_enabled()) \
      _trace_printf(__VA_ARGS__); \
  } while (0)

static const gchar *
_static_type_name(FilterXStaticType static_type)
{
  switch (static_type)
    {
    case FILTERX_STATIC_TYPE_DICT:
      return "DICT";
    case FILTERX_STATIC_TYPE_LIST:
      return "LIST";
    case FILTERX_STATIC_TYPE_STRING:
      return "STRING";
    case FILTERX_STATIC_TYPE_INTEGER:
      return "INTEGER";
    case FILTERX_STATIC_TYPE_DOUBLE:
      return "DOUBLE";
    case FILTERX_STATIC_TYPE_BOOLEAN:
      return "BOOLEAN";
    case FILTERX_STATIC_TYPE_UNKNOWN:
    default:
      return "UNKNOWN";
    }
}

/* rotating buffers, so that one _trace() can name two paths */
static const gchar *
_format_path(const FilterXAccessPath *path)
{
  static gchar buffers[4][512];
  static guint next = 0;

  gchar *buf = buffers[next++ % G_N_ELEMENTS(buffers)];
  gsize offset = 0;

  /* no real variable maps onto handle 0, which is where the dict merge roots its scratch env */
  if (path->root == 0)
    offset += g_snprintf(buf + offset, sizeof(buffers[0]) - offset, "<rhs>");
  else
    {
      const gchar *name = log_msg_get_handle_name(filterx_variable_handle_to_nv_handle(path->root), NULL);
      gboolean message_tied = (path->root & FILTERX_HANDLE_FLOATING_BIT) == 0;
      offset += g_snprintf(buf + offset, sizeof(buffers[0]) - offset, "%s%s", message_tied ? "$" : "", name ? : "?");
    }

  for (guint i = 0; i < path->n_steps; i++)
    offset += g_snprintf(buf + offset, sizeof(buffers[0]) - offset, ".%s", path->steps[i]);

  if (path->truncated)
    g_snprintf(buf + offset, sizeof(buffers[0]) - offset, "...");

  return buf;
}

void
filterx_type_inference_trace_banner(const gchar *format, ...)
{
  if (!filterx_type_inference_trace_enabled())
    return;

  va_list ap;

  fputc('\n', stderr);
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fputc('\n', stderr);
}

static gboolean
_trace_dump_entry(gpointer key, gpointer value, gpointer user_data)
{
  const FilterXAccessPath *path = (const FilterXAccessPath *) key;

  fprintf(stderr, "      %-28s %-8s%s\n", _format_path(path), _static_type_name(_fact_static_type(value)),
          _fact_is_closed(value) ? " closed" : "");
  return FALSE;
}

void
filterx_type_env_trace_dump(const FilterXTypeEnv *self, const gchar *label)
{
  if (!filterx_type_inference_trace_enabled())
    return;

  fprintf(stderr, "  ---- env after: %s\n", label ? : "");
  if (g_tree_nnodes(self->facts) == 0)
    fprintf(stderr, "      (empty)\n");
  g_tree_foreach(self->facts, _trace_dump_entry, NULL);
}

/* --- the lattice ---------------------------------------------------------------------------- */

static inline gboolean
_is_numeric_static_type(FilterXStaticType static_type)
{
  return static_type == FILTERX_STATIC_TYPE_INTEGER || static_type == FILTERX_STATIC_TYPE_DOUBLE;
}

FilterXStaticType
filterx_static_type_numeric_promote(FilterXStaticType a, FilterXStaticType b)
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
  self->facts = g_tree_new_full(filterx_access_path_compare, NULL, g_free, NULL);
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
_lookup(const FilterXTypeEnv *self, const FilterXAccessPath *path)
{
  return g_tree_lookup(self->facts, path);
}

static inline void
_insert(FilterXTypeEnv *self, const FilterXAccessPath *path, gpointer fact)
{
  g_tree_insert(self->facts, filterx_access_path_dup(path), fact);
}

gboolean
filterx_type_env_get_fact_at_path(const FilterXTypeEnv *self, const FilterXAccessPath *path,
                                  FilterXStaticType *static_type_out, gboolean *closed_out)
{
  gpointer fact = _lookup(self, path);
  if (!fact)
    return FALSE;

  if (static_type_out)
    *static_type_out = _fact_static_type(fact);
  if (closed_out)
    *closed_out = _fact_is_closed(fact);
  return TRUE;
}

/* The skip to @root stays linear: g_tree_lower_bound()/g_tree_node_next() need GLib 2.68 and the
 * minimum here is 2.32.  The env holds tens of entries and lives for one pipe init. */
typedef gboolean (*FilterXTypeRangeFunc)(const FilterXAccessPath *path, gpointer fact, gpointer user_data);

typedef struct
{
  const FilterXAccessPath *root;
  FilterXTypeRangeFunc func;
  gpointer user_data;
} _RangeCtx;

static gboolean
_range_traverse(gpointer key, gpointer value, gpointer user_data)
{
  _RangeCtx *ctx = (_RangeCtx *) user_data;
  const FilterXAccessPath *path = (const FilterXAccessPath *) key;

  if (filterx_access_path_compare(path, ctx->root, NULL) < 0)
    return FALSE;                                        /* not there yet */

  if (!filterx_access_path_is_prefix_of(ctx->root, path))
    return TRUE;                                         /* past the subtree, stop */

  return ctx->func(path, value, ctx->user_data);
}

static void
_range_foreach(const FilterXTypeEnv *self, const FilterXAccessPath *root,
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
  GPtrArray *paths;    /* owned FilterXAccessPath * */
  GArray *facts;       /* the packed facts, index-aligned with @paths */
  guint depth_filter;  /* when non-zero, only paths of exactly this many steps are collected */
} _Snapshot;

static gboolean
_snapshot_collect(const FilterXAccessPath *path, gpointer fact, gpointer user_data)
{
  _Snapshot *snap = (_Snapshot *) user_data;

  if (snap->depth_filter && path->n_steps != snap->depth_filter)
    return FALSE;

  g_ptr_array_add(snap->paths, filterx_access_path_dup(path));
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
_snapshot_range(const FilterXTypeEnv *self, const FilterXAccessPath *root, _Snapshot *snap)
{
  _snapshot_init(snap);
  _range_foreach(self, root, _snapshot_collect, snap);
}

static void
_snapshot_direct_children(const FilterXTypeEnv *self, const FilterXAccessPath *parent, _Snapshot *snap)
{
  _snapshot_init(snap);
  snap->depth_filter = parent->n_steps + 1;
  _range_foreach(self, parent, _snapshot_collect, snap);
}

static inline const FilterXAccessPath *
_snapshot_path(const _Snapshot *snap, guint i)
{
  return (const FilterXAccessPath *) g_ptr_array_index(snap->paths, i);
}

static inline gpointer
_snapshot_fact(const _Snapshot *snap, guint i)
{
  return g_array_index(snap->facts, gpointer, i);
}

static void
_drop_range(FilterXTypeEnv *self, const FilterXAccessPath *root, gboolean include_root)
{
  _Snapshot snap;

  _snapshot_range(self, root, &snap);
  for (guint i = 0; i < snap.paths->len; i++)
    {
      const FilterXAccessPath *path = _snapshot_path(&snap, i);
      if (!include_root && filterx_access_path_compare(path, root, NULL) == 0)
        continue;
      _trace("|   drop %s (was %s)", _format_path(path),
             _static_type_name(_fact_static_type(_snapshot_fact(&snap, i))));
      g_tree_remove(self->facts, path);
    }
  _snapshot_clear(&snap);
}

static void
_mark_not_closed(FilterXTypeEnv *self, const FilterXAccessPath *path)
{
  gpointer fact = _lookup(self, path);
  if (fact && _fact_is_closed(fact))
    _insert(self, path, _fact_pack(_fact_static_type(fact), FALSE));
}

/* --- reading -------------------------------------------------------------------------------- */

FilterXStaticType
filterx_type_env_get_static_type_at_path(const FilterXTypeEnv *self, const FilterXAccessPath *path)
{
  if (path->truncated)
    return FILTERX_STATIC_TYPE_UNKNOWN;

  gpointer fact = _lookup(self, path);
  return fact ? _fact_static_type(fact) : FILTERX_STATIC_TYPE_UNKNOWN;
}

FilterXStaticType
filterx_type_env_get_static_type_of_expr(const FilterXTypeEnv *self, FilterXExpr *expr)
{
  FilterXAccessPath path;

  if (!filterx_expr_get_path(expr, &path))
    {
      _trace("| read %s: no addressable path -> UNKNOWN", expr ? expr->type : "(none)");
      return FILTERX_STATIC_TYPE_UNKNOWN;
    }

  FilterXStaticType static_type = filterx_type_env_get_static_type_at_path(self, &path);

  _trace("| read %s -> %s", _format_path(&path), _static_type_name(static_type));
  return static_type;
}

/* --- writing -------------------------------------------------------------------------------- */

void
filterx_type_env_set_at_path(FilterXTypeEnv *self, const FilterXAccessPath *path,
                             FilterXStaticType static_type, gboolean closed)
{
  if (path->truncated)
    return;

  _trace("| set %s <- %s%s", _format_path(path), _static_type_name(static_type), closed ? " closed" : "");

  /* a closed parent stays closed: the new key is a recorded child of its own now */
  _drop_range(self, path, TRUE);
  _insert(self, path, _fact_pack(static_type, closed));
}

void
filterx_type_env_open_at_path(FilterXTypeEnv *self, const FilterXAccessPath *path)
{
  if (path->truncated)
    return;

  _trace("| open %s", _format_path(path));

  gpointer fact = _lookup(self, path);
  _drop_range(self, path, FALSE);

  if (fact)
    _insert(self, path, _fact_pack(_fact_static_type(fact), FALSE));
}

/* --- shapes --------------------------------------------------------------------------------- */

FilterXStaticType
filterx_static_type_from_object(FilterXObject *obj)
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
  const FilterXAccessPath *base;
  gboolean all_keys_recorded;
} _ObjectShapeCtx;

static void _install_shape_from_object(FilterXTypeEnv *env, const FilterXAccessPath *base, FilterXObject *obj);

static gboolean
_install_element_shape(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  _ObjectShapeCtx *ctx = (_ObjectShapeCtx *) user_data;

  /* a list yields indices, and filterx_sequence_normalize_index() resolves those against the
   * runtime length, so l[-1] and l[0] can name the same slot and neither names a step */
  const gchar *step = NULL;
  if (key && filterx_object_is_type_or_ref(key, &FILTERX_TYPE_NAME(string)))
    step = filterx_access_path_intern_key(filterx_string_get_value_ref_and_assert_nul(key, NULL));

  FilterXAccessPath child = *ctx->base;
  if (!filterx_access_path_append_step(&child, step))
    {
      ctx->all_keys_recorded = FALSE;
      return TRUE;
    }

  _install_shape_from_object(ctx->env, &child, value);
  return TRUE;
}

static void
_install_shape_from_object(FilterXTypeEnv *env, const FilterXAccessPath *base, FilterXObject *obj)
{
  FilterXStaticType static_type = filterx_static_type_from_object(obj);

  _trace("| shape of %s from %s -> %s", _format_path(base),
         obj ? filterx_object_get_type_name(obj) : "(null)", _static_type_name(static_type));

  if (static_type != FILTERX_STATIC_TYPE_DICT && static_type != FILTERX_STATIC_TYPE_LIST)
    {
      filterx_type_env_set_at_path(env, base, static_type, FALSE);
      return;
    }

  /* An object in hand has an exactly known key set, so it starts out closed.  It only stays
   * closed if every key made it into a child path: closed with no children claims the container
   * is empty, which is stronger than the key merely having gone unrecorded. */
  filterx_type_env_set_at_path(env, base, static_type, TRUE);

  _ObjectShapeCtx ctx = { .env = env, .base = base, .all_keys_recorded = TRUE };
  filterx_object_iter(obj, _install_element_shape, &ctx);

  if (!ctx.all_keys_recorded)
    _mark_not_closed(env, base);
}

/* Copy range(@src) out of @src_env and re-root it at @dst in @dst_env.  The two envs may be the
 * same one and the two ranges may overlap, `d.sub = d` and `d = d.sub` both being real
 * statements, so the source is snapshotted before the destination is dropped. */
static void
_install_shape_from_tracked_path(FilterXTypeEnv *dst_env, const FilterXAccessPath *dst,
                                 const FilterXTypeEnv *src_env, const FilterXAccessPath *src)
{
  _Snapshot snap;

  _trace("| copy %s <- %s", _format_path(dst), _format_path(src));

  _snapshot_range(src_env, src, &snap);
  _drop_range(dst_env, dst, TRUE);

  /* the destination exists even when the source range is empty, and an entry is the only thing
   * that says so: a closed parent would otherwise read the key just written as proven absent */
  _insert(dst_env, dst, _fact_pack(FILTERX_STATIC_TYPE_UNKNOWN, FALSE));

  for (guint i = 0; i < snap.paths->len; i++)
    {
      const FilterXAccessPath *path = _snapshot_path(&snap, i);
      gpointer fact = _snapshot_fact(&snap, i);

      FilterXAccessPath rerooted = *dst;
      rerooted.truncated = FALSE;
      gboolean fits = TRUE;
      for (guint step = src->n_steps; step < path->n_steps && fits; step++)
        fits = filterx_access_path_append_step(&rerooted, path->steps[step]);

      if (!fits)
        {
          /* the copy ran out of depth, so the destination gains a key it has no entry for */
          rerooted.truncated = FALSE;
          _mark_not_closed(dst_env, &rerooted);
          continue;
        }

      _trace("|   %s <- %s%s", _format_path(&rerooted), _static_type_name(_fact_static_type(fact)),
             _fact_is_closed(fact) ? " closed" : "");
      _insert(dst_env, &rerooted, fact);
    }

  _snapshot_clear(&snap);
}

/* where the shape of a written value comes from, when it comes from anywhere */
typedef enum
{
  SHAPE_STATIC_TYPE_ONLY,
  SHAPE_FROM_OBJECT,          /* a folded literal, or a sparse literal container */
  SHAPE_FROM_TRACKED_PATH,    /* a read of a location this env already records */
} _ShapeSource;

typedef struct
{
  _ShapeSource source;
  FilterXStaticType static_type;
  FilterXObject *object;          /* SHAPE_FROM_OBJECT, owned when @object_owned */
  gboolean object_owned;
  FilterXExpr *container_expr;    /* a literal container, whose non-literal elements need typing */
  FilterXAccessPath src_path;       /* SHAPE_FROM_TRACKED_PATH */
} _Shape;

static _Shape
_shape_of(FilterXExpr *rhs_expr)
{
  _Shape shape = { .source = SHAPE_STATIC_TYPE_ONLY, .static_type = FILTERX_STATIC_TYPE_UNKNOWN };

  if (!rhs_expr)
    return shape;

  shape.static_type = rhs_expr->static_type;

  if (filterx_expr_is_literal(rhs_expr))
    {
      shape.source = SHAPE_FROM_OBJECT;
      shape.object = filterx_literal_get_value(rhs_expr);
      shape.object_owned = TRUE;
      return shape;
    }

  if (filterx_expr_is_literal_container(rhs_expr))
    {
      shape.container_expr = rhs_expr;
      shape.object = filterx_literal_container_get_sparse_container(rhs_expr);
      if (shape.object)
        shape.source = SHAPE_FROM_OBJECT;
      return shape;
    }

  if (filterx_expr_get_path(rhs_expr, &shape.src_path) && !shape.src_path.truncated)
    shape.source = SHAPE_FROM_TRACKED_PATH;

  return shape;
}

static const gchar *
_shape_source_name(_ShapeSource source)
{
  switch (source)
    {
    case SHAPE_FROM_OBJECT:
      return "from object";
    case SHAPE_FROM_TRACKED_PATH:
      return "from tracked path";
    case SHAPE_STATIC_TYPE_ONLY:
    default:
      return "static type only";
    }
}

static void
_clear_shape(_Shape *shape)
{
  if (shape->object_owned)
    filterx_object_unref(shape->object);
}

static void
_install_shape(FilterXTypeEnv *self, const FilterXAccessPath *path, _Shape *shape)
{
  switch (shape->source)
    {
    case SHAPE_FROM_OBJECT:
      _install_shape_from_object(self, path, shape->object);
      break;
    case SHAPE_FROM_TRACKED_PATH:
      _install_shape_from_tracked_path(self, path, self, &shape->src_path);
      break;
    case SHAPE_STATIC_TYPE_ONLY:
    default:
      filterx_type_env_set_at_path(self, path, shape->static_type, FALSE);
      break;
    }

  if (shape->container_expr && !filterx_literal_container_infer_nonliteral_elements(shape->container_expr, self, path))
    _mark_not_closed(self, path);
}

/* A truncated path names no location to act on, so the deepest ancestor it does address is what
 * the env can still speak about.  `d = {}; d[$k] = "s";` leaves d a DICT with no leaves, which is
 * what makes the `d.a = 1` after it the only thing d.a can be. */
static void
_open_deepest_addressable_ancestor(FilterXTypeEnv *self, const FilterXAccessPath *path)
{
  FilterXAccessPath ancestor = *path;
  ancestor.truncated = FALSE;
  filterx_type_env_open_at_path(self, &ancestor);
}

void
filterx_type_env_set_shape_at_path(FilterXTypeEnv *self, const FilterXAccessPath *path, FilterXExpr *rhs_expr)
{
  if (path->truncated)
    {
      _open_deepest_addressable_ancestor(self, path);
      return;
    }

  _Shape shape = _shape_of(rhs_expr);

  _trace("| write %s <- %s (%s, %s)", _format_path(path), rhs_expr ? rhs_expr->type : "(none)",
         _shape_source_name(shape.source), _static_type_name(shape.static_type));
  _trace_depth++;

  _install_shape(self, path, &shape);

  _trace_depth--;
  _clear_shape(&shape);
}

void
filterx_type_env_clear_at_path(FilterXTypeEnv *self, const FilterXAccessPath *path)
{
  if (path->truncated)
    {
      _open_deepest_addressable_ancestor(self, path);
      return;
    }

  _trace("| clear %s", _format_path(path));

  /* the parent keeps `closed`: a named key proven gone only shrinks its key set */
  _drop_range(self, path, TRUE);
}

/* --- the expression-level entry points ------------------------------------------------------ */

void
filterx_type_env_update_on_write(FilterXTypeEnv *self, FilterXExpr *target_expr, FilterXExpr *rhs_expr)
{
  FilterXAccessPath path;

  if (filterx_expr_get_path(target_expr, &path))
    {
      filterx_type_env_set_shape_at_path(self, &path, rhs_expr);
      return;
    }

  /* A dpath resolves its elements at runtime, so its root variable is the deepest location this
   * pass can name for the write.  The root keeps its static type: the write reaches into the
   * container rather than replacing it. */
  FilterXExpr *dpath_root = filterx_dpath_lvalue_get_variable(target_expr);
  if (dpath_root && filterx_expr_get_path(dpath_root, &path))
    filterx_type_env_open_at_path(self, &path);
}

void
filterx_type_env_update_on_optional_write(FilterXTypeEnv *self, FilterXExpr *target_expr, FilterXExpr *rhs_expr)
{
  FilterXTypeEnv *if_written = filterx_type_env_clone(self);

  filterx_type_env_update_on_write(if_written, target_expr, rhs_expr);

  filterx_type_env_meet_into(self, if_written);
  filterx_type_env_free(if_written);
}

void
filterx_type_env_update_on_remove(FilterXTypeEnv *self, FilterXExpr *target_expr)
{
  FilterXAccessPath path;

  if (!target_expr || !filterx_expr_get_path(target_expr, &path))
    return;

  filterx_type_env_clear_at_path(self, &path);
}

/* what `target += rhs` leaves in target */
static FilterXStaticType
_plus_assign_result(FilterXStaticType target, FilterXStaticType rhs)
{
  FilterXStaticType promoted = filterx_static_type_numeric_promote(target, rhs);
  if (promoted != FILTERX_STATIC_TYPE_UNKNOWN)
    return promoted;

  gboolean merges_in_place = (target == rhs &&
                              (target == FILTERX_STATIC_TYPE_DICT ||
                               target == FILTERX_STATIC_TYPE_LIST ||
                               target == FILTERX_STATIC_TYPE_STRING));
  return merges_in_place ? target : FILTERX_STATIC_TYPE_UNKNOWN;
}

/* The RHS may be a range of the very env being written, and the per-key overwrite would otherwise
 * read entries it has already replaced. */
static FilterXTypeEnv *
_materialize_shape_in_scratch_env(FilterXTypeEnv *self, _Shape *shape, const FilterXAccessPath *root)
{
  FilterXTypeEnv *scratch = filterx_type_env_new();

  if (shape->source == SHAPE_FROM_TRACKED_PATH)
    _install_shape_from_tracked_path(scratch, root, self, &shape->src_path);
  else
    _install_shape(scratch, root, shape);

  return scratch;
}

/* filterx_mapping_merge() is a shallow per-key overwrite: the RHS's keys replace the target's and
 * the target's other keys survive.  FALSE when the RHS is not closed, its key set being one this
 * pass cannot enumerate. */
static gboolean
_merge_dict_keys_into_path(FilterXTypeEnv *self, const FilterXAccessPath *path, _Shape *rhs_shape)
{
  FilterXAccessPath scratch_root = { .root = 0, .n_steps = 0 };
  FilterXTypeEnv *scratch = _materialize_shape_in_scratch_env(self, rhs_shape, &scratch_root);

  gpointer rhs_fact = _lookup(scratch, &scratch_root);
  if (!rhs_fact || !_fact_is_closed(rhs_fact))
    {
      filterx_type_env_free(scratch);
      return FALSE;
    }

  _Snapshot children;
  _snapshot_direct_children(scratch, &scratch_root, &children);

  for (guint i = 0; i < children.paths->len; i++)
    {
      const FilterXAccessPath *child = _snapshot_path(&children, i);

      FilterXAccessPath dst = *path;
      if (!filterx_access_path_append_step(&dst, child->steps[child->n_steps - 1]))
        {
          /* the key the merge brings in lands past the depth cap, so the target gains one it has
           * no entry for */
          _mark_not_closed(self, path);
          continue;
        }
      dst.truncated = FALSE;

      _install_shape_from_tracked_path(self, &dst, scratch, child);
    }

  _snapshot_clear(&children);
  filterx_type_env_free(scratch);
  return TRUE;
}

FilterXStaticType
filterx_type_env_update_on_plus_assign(FilterXTypeEnv *self, FilterXExpr *target_expr, FilterXExpr *rhs_expr)
{
  FilterXAccessPath path;

  if (!target_expr || !filterx_expr_get_path(target_expr, &path))
    return FILTERX_STATIC_TYPE_UNKNOWN;

  if (path.truncated)
    {
      _open_deepest_addressable_ancestor(self, &path);
      return FILTERX_STATIC_TYPE_UNKNOWN;
    }

  FilterXStaticType target = filterx_type_env_get_static_type_at_path(self, &path);
  FilterXStaticType rhs = rhs_expr ? rhs_expr->static_type : FILTERX_STATIC_TYPE_UNKNOWN;
  FilterXStaticType result = _plus_assign_result(target, rhs);

  _trace("| merge %s: %s += %s -> %s", _format_path(&path), _static_type_name(target),
         _static_type_name(rhs), _static_type_name(result));

  if (result != FILTERX_STATIC_TYPE_DICT)
    {
      filterx_type_env_set_at_path(self, &path, result, FALSE);
      return result;
    }

  gpointer target_fact = _lookup(self, &path);
  if (!target_fact)
    return result;

  gboolean target_closed = _fact_is_closed(target_fact);
  _Shape shape = _shape_of(rhs_expr);

  /* only a dict merge is per-key; a list or string concatenation says nothing about where the
   * target's own keys ended up */
  gboolean merged_per_key = (_fact_static_type(target_fact) == FILTERX_STATIC_TYPE_DICT &&
                             shape.static_type == FILTERX_STATIC_TYPE_DICT &&
                             _merge_dict_keys_into_path(self, &path, &shape));

  if (!merged_per_key)
    filterx_type_env_open_at_path(self, &path);
  else
    {
      /* the RHS's keys are all accounted for above, so only the target's own key set still bounds
       * the result */
      _trace("| set %s <- DICT%s", _format_path(&path), target_closed ? " closed" : "");
      _insert(self, &path, _fact_pack(FILTERX_STATIC_TYPE_DICT, target_closed));
    }

  _clear_shape(&shape);
  return result;
}

static gboolean
_open_argument(FilterXExpr *parent, FilterXExpr **child, gpointer user_data)
{
  FilterXTypeEnv *env = (FilterXTypeEnv *) user_data;
  FilterXAccessPath path;

  if (*child && filterx_expr_get_path(*child, &path))
    {
      _open_deepest_addressable_ancestor(env, &path);
    }

  return TRUE;
}

void
filterx_type_env_open_arguments(FilterXTypeEnv *self, FilterXExpr *expr)
{
  filterx_expr_walk_children(expr, _open_argument, self);
}

/* --- the join ------------------------------------------------------------------------------- */

static gboolean
_collect_entry(gpointer key, gpointer value, gpointer user_data)
{
  _Snapshot *snap = (_Snapshot *) user_data;

  g_ptr_array_add(snap->paths, filterx_access_path_dup((const FilterXAccessPath *) key));
  g_array_append_val(snap->facts, value);
  return FALSE;
}

static void
_snapshot_all(const FilterXTypeEnv *self, _Snapshot *snap)
{
  _snapshot_init(snap);
  g_tree_foreach(self->facts, _collect_entry, snap);
}

static gboolean
_deepest_recorded_ancestor(const FilterXTypeEnv *dst, const FilterXAccessPath *path, FilterXAccessPath *out)
{
  FilterXAccessPath probe = *path;

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

/* A key src does not have survives only where src proves it absent, which a closed parent does:
 * an absent key reads as C NULL, so src constrains nothing.  That is what keeps d.a INTEGER after
 * `d = {}; if (c) { d.a = 1; }`. */
static void
_meet_entry_src_does_not_have(FilterXTypeEnv *dst, const FilterXAccessPath *path, const FilterXTypeEnv *src)
{
  if (path->n_steps == 0)
    {
      /* a variable missing from src is one src knows nothing about, not one it proved unset */
      g_tree_remove(dst->facts, path);
      return;
    }

  FilterXAccessPath parent;
  filterx_access_path_parent(path, &parent);

  gpointer src_parent = _lookup(src, &parent);
  if (src_parent && _fact_is_closed(src_parent))
    return;

  /* src may hold this key at a type nothing here has seen, and the parent's key set counted the
   * entry about to go */
  g_tree_remove(dst->facts, path);
  _mark_not_closed(dst, &parent);
}

static void
_meet_dst_entries_against_src(FilterXTypeEnv *dst, const FilterXTypeEnv *src)
{
  _Snapshot snap;

  _snapshot_all(dst, &snap);

  for (guint i = 0; i < snap.paths->len; i++)
    {
      const FilterXAccessPath *path = _snapshot_path(&snap, i);
      gpointer dst_fact = _snapshot_fact(&snap, i);
      gpointer src_fact = _lookup(src, path);

      if (!src_fact)
        {
          _meet_entry_src_does_not_have(dst, path, src);
          continue;
        }

      /* both sides claim the value exists, so the entry stays even where the static types
       * disagree: dropping it would report a key of a closed container as absent */
      _insert(dst, path, _fact_pack(filterx_static_type_meet(_fact_static_type(dst_fact), _fact_static_type(src_fact)),
                                    _fact_is_closed(dst_fact) && _fact_is_closed(src_fact)));
    }

  _snapshot_clear(&snap);
}

/* a key src knows about and dst does not breaks dst's claim to a complete key set */
static void
_open_dst_ancestors_of_src_only_keys(FilterXTypeEnv *dst, const FilterXTypeEnv *src)
{
  _Snapshot snap;

  _snapshot_all(src, &snap);

  for (guint i = 0; i < snap.paths->len; i++)
    {
      const FilterXAccessPath *path = _snapshot_path(&snap, i);

      if (_lookup(dst, path))
        continue;

      FilterXAccessPath ancestor;
      if (_deepest_recorded_ancestor(dst, path, &ancestor))
        _mark_not_closed(dst, &ancestor);
    }

  _snapshot_clear(&snap);
}

void
filterx_type_env_meet_into(FilterXTypeEnv *dst, const FilterXTypeEnv *src)
{
  _meet_dst_entries_against_src(dst, src);
  _open_dst_ancestors_of_src_only_keys(dst, src);
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
  const FilterXAccessPath *path = (const FilterXAccessPath *) key;

  if (!ctx->pred || ctx->pred(path->root, ctx->user_data))
    g_tree_insert(ctx->clone->facts, filterx_access_path_dup(path), value);
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

  _trace("+ %s%s%s", self->type ? : "?", self->name ? " " : "", self->name ? : "");
  _trace_depth++;

  if (self->infer_types)
    self->infer_types(self, env);
  else
    filterx_expr_infer_types_default(self, env);

  _trace_depth--;
  _trace("= %s -> %s", self->type ? : "?", _static_type_name(self->static_type));

  self->types_inferred = TRUE;
#endif
}

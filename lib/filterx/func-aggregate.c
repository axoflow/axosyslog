/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/func-aggregate.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "filterx/object-dict.h"
#include "filterx/object-tuple.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"

static FilterXObject *
_agg_sum(FilterXObject *existing, FilterXObject *incoming)
{
  if (existing)
    return filterx_object_add(existing, incoming);
  return filterx_object_dup(incoming);
}

typedef struct
{
  FilterXObject *target;
} AggregateMergeContext;

static gboolean
_aggregate_elem(FilterXObject *key, FilterXObject *incoming, gpointer user_data)
{
  AggregateMergeContext *ctx = (AggregateMergeContext *) user_data;

  FilterXObject *existing = filterx_object_get_subscript(ctx->target, key);
  FilterXObject *new_value = _agg_sum(existing, incoming);
  filterx_object_unref(existing);

  if (!new_value)
    return FALSE;

  FilterXObject *durable_key = filterx_object_ref(key);
  filterx_eval_retain_object(&durable_key, FX_RETAIN_DECOUPLE_CONFIG);

  gboolean ok = filterx_object_set_subscript(ctx->target, durable_key, &new_value);
  filterx_object_unref(durable_key);
  filterx_object_unref(new_value);
  return ok;
}

static gboolean
_aggregate_merge(FilterXObject *target, FilterXObject *incoming_values)
{
  if (!filterx_object_is_type_or_ref(incoming_values, &FILTERX_TYPE_NAME(mapping)))
    return FALSE;

  AggregateMergeContext ctx = { .target = target };
  return filterx_object_iter(incoming_values, _aggregate_elem, &ctx);
}

#define FILTERX_FUNC_AGGREGATE_USAGE "Usage: aggregate(key=expr, values=dict, [close=expr])"

/* aggregate() returns a (status, values) tuple */
#define FILTERX_FUNC_AGGREGATE_STATUS_ABSORBED "absorbed"
#define FILTERX_FUNC_AGGREGATE_STATUS_CLOSED "closed"

static FilterXObject *
_wrap_result(const gchar *status, FilterXObject *values)
{
  FilterXObject *result = filterx_tuple_new(2);

  FilterXObject *status_obj = filterx_string_new(status, -1);
  filterx_tuple_set_subscript(result, 0, status_obj);
  filterx_object_unref(status_obj);

  filterx_tuple_set_subscript(result, 1, values);
  filterx_object_unref(values);

  return result;
}

static guint
_tuple_key_hash(gconstpointer p)
{
  return filterx_object_hash((FilterXObject *) p);
}

static gboolean
_tuple_key_equal(gconstpointer a, gconstpointer b)
{
  return filterx_object_equal((FilterXObject *) a, (FilterXObject *) b);
}

/*
 * AggregateSharedState: encapsulate all state related to an aggregate()
 * invocation, potentially accessed from multiple threads (main, and various
 * worker threads).
 */
typedef struct _AggregateSharedState
{
  GAtomicCounter ref_cnt;
  GMutex lock;
  GHashTable *entries;            /* tuple_key (ref) -> AggregateEntry* (ref) */
} AggregateSharedState;

/*
 * AggregateEntry: per-key state
 *
 * Created the first time a key is seen.  Accessed potentially from multiple
 * threads (e.g.  multiple aggregate invocations, as well as the timer on
 * the main thread).
 */
typedef struct _AggregateEntry
{
  GAtomicCounter ref_cnt;
  AggregateSharedState *shared;
  FilterXObject *tuple_key;
  FilterXObject *values;
  gboolean closed;
} AggregateEntry;

static AggregateEntry *_aggregate_entry_ref(AggregateEntry *entry);
static void _aggregate_entry_unref(AggregateEntry *entry);

static AggregateSharedState *
_shared_state_ref(AggregateSharedState *shared)
{
  g_atomic_counter_inc(&shared->ref_cnt);
  return shared;
}

static void
_shared_state_unref(AggregateSharedState *shared)
{
  if (!shared)
    return;

  if (g_atomic_counter_dec_and_test(&shared->ref_cnt))
    {
      if (shared->entries)
        g_hash_table_unref(shared->entries);
      g_mutex_clear(&shared->lock);
      g_free(shared);
    }
}

static AggregateSharedState *
_shared_state_new(void)
{
  AggregateSharedState *shared = g_new0(AggregateSharedState, 1);
  g_atomic_counter_set(&shared->ref_cnt, 1);
  g_mutex_init(&shared->lock);

  shared->entries = g_hash_table_new_full(_tuple_key_hash, _tuple_key_equal,
                                          (GDestroyNotify) filterx_object_unref,
                                          (GDestroyNotify) _aggregate_entry_unref);

  return shared;
}

static AggregateEntry *
_aggregate_entry_ref(AggregateEntry *entry)
{
  g_atomic_counter_inc(&entry->ref_cnt);
  return entry;
}

static void
_aggregate_entry_unref(AggregateEntry *entry)
{
  if (g_atomic_counter_dec_and_test(&entry->ref_cnt))
    {
      filterx_object_unref(entry->tuple_key);
      filterx_object_unref(entry->values);
      _shared_state_unref(entry->shared);
      g_free(entry);
    }
}

static void
_close_entry(AggregateSharedState *shared, AggregateEntry *entry)
{
  entry->closed = TRUE;
  g_hash_table_remove(shared->entries, entry->tuple_key);
}

static AggregateEntry *
_new_entry(AggregateSharedState *shared, FilterXObject *tuple_key)
{
  AggregateEntry *entry = g_new0(AggregateEntry, 1);

  g_atomic_counter_set(&entry->ref_cnt, 1);
  entry->shared = _shared_state_ref(shared);
  entry->tuple_key = filterx_object_ref(tuple_key);
  entry->values = filterx_dict_new();
  filterx_object_cow_prepare(&entry->values);

  g_hash_table_insert(shared->entries, filterx_object_ref(tuple_key), entry);
  return entry;
}

static FilterXObject *
_aggregate(AggregateSharedState *shared, FilterXObject *tuple_key, FilterXObject *values, gboolean close)
{
  AggregateEntry *entry = g_hash_table_lookup(shared->entries, tuple_key);
  if (!entry)
    entry = _new_entry(shared, tuple_key);

  if (!_aggregate_merge(entry->values, values))
    {
      filterx_eval_push_error("aggregate(): failed to merge values", values);
      return NULL;
    }

  FilterXObject *merged_values = filterx_ref_float(filterx_object_copy(entry->values));

  if (close)
    {
      _close_entry(shared, entry);
      return _wrap_result(FILTERX_FUNC_AGGREGATE_STATUS_CLOSED, merged_values);
    }
  return _wrap_result(FILTERX_FUNC_AGGREGATE_STATUS_ABSORBED, merged_values);
}

typedef struct FilterXFunctionAggregate_
{
  FilterXFunction super;
  FilterXExpr *key_expr;
  FilterXExpr *values_expr;
  FilterXExpr *close_expr;
  AggregateSharedState *shared;
} FilterXFunctionAggregate;

static FilterXObject *
_normalize_key_as_tuple(FilterXObject *key)
{
  if (filterx_object_is_type_or_ref(key, &FILTERX_TYPE_NAME(tuple)))
    {
      return filterx_object_dup(key);
    }

  FilterXObject *tuple_key = filterx_tuple_new(1);
  FilterXObject *dupped_key = filterx_object_dup(key);
  filterx_tuple_set_subscript(tuple_key, 0, dupped_key);
  filterx_object_unref(dupped_key);
  return tuple_key;
}


static FilterXObject *
_eval_fx_aggregate(FilterXExpr *s)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;
  FilterXObject *result = NULL;
  FilterXObject *key = NULL;
  FilterXObject *values = NULL;
  FilterXObject *close_value = NULL;

  key = filterx_expr_eval_typed(self->key_expr);
  if (!key)
    goto exit;

  values = filterx_expr_eval_typed(self->values_expr);
  if (!values)
    goto exit;

  if (!filterx_object_is_type_or_ref(values, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_eval_push_error("aggregate(): values must be a dict", values);
      goto exit;
    }

  gboolean close = FALSE;
  if (self->close_expr)
    {
      close_value = filterx_expr_eval_typed(self->close_expr);
      if (!close_value)
        goto exit;
      close = filterx_object_truthy(close_value);
    }

  gpointer allocator_state;
  filterx_eval_disable_allocator(&allocator_state);

  FilterXObject *tuple_key = _normalize_key_as_tuple(key);

  g_mutex_lock(&self->shared->lock);
  result = _aggregate(self->shared, tuple_key, values, close);
  g_mutex_unlock(&self->shared->lock);

  filterx_object_unref(tuple_key);

  filterx_eval_restore_allocator(&allocator_state);

exit:
  filterx_object_unref(close_value);
  filterx_object_unref(key);
  filterx_object_unref(values);
  return result;
}
static gboolean
_aggregate_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;


  return filterx_function_init_method(&self->super, cfg);
}

static void
_aggregate_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;

  g_hash_table_remove_all(self->shared->entries);
  filterx_function_deinit_method(&self->super, cfg);
}

static gboolean
_aggregate_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;

  FilterXExpr **exprs[] = { &self->key_expr, &self->values_expr, &self->close_expr };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

static gboolean
_extract_args(FilterXFunctionAggregate *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }

  self->key_expr = filterx_function_args_get_named_expr(args, "key");
  if (!self->key_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "key argument is required. " FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }

  self->values_expr = filterx_function_args_get_named_expr(args, "values");
  if (!self->values_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "values argument is required. " FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }
  self->close_expr = filterx_function_args_get_named_expr(args, "close");

  return TRUE;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;

  filterx_expr_unref(self->key_expr);
  filterx_expr_unref(self->values_expr);
  filterx_expr_unref(self->close_expr);
  _shared_state_unref(self->shared);
  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_function_aggregate_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionAggregate *self = g_new0(FilterXFunctionAggregate, 1);

  filterx_function_init_instance(&self->super, "aggregate", FXE_WORLD);
  self->super.super.eval = _eval_fx_aggregate;
  self->super.super.init = _aggregate_init;
  self->super.super.deinit = _aggregate_deinit;
  self->super.super.walk_children = _aggregate_walk;
  self->super.super.free_fn = _free;

  if (!_extract_args(self, args, error) || !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  self->shared = _shared_state_new();
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

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
#include "filterx/expr-literal.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "filterx/object-dict.h"
#include "filterx/object-tuple.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-pipe.h"
#include "scratch-buffers.h"
#include "ml-batched-timer.h"
#include "mainloop.h"
#include "generic-number.h"
#include "parse-number.h"

/*
 * Aggregator functions: combine an existing accumulated value with an
 * incoming new value.  `existing` is NULL when a key is seen for the
 * first time.  The returned object is owned by the caller.
 *
 * `aux_state` is a per-(entry, field) storage slot an aggregator can use to
 * keep bookkeeping data that doesn't fit into the displayed accumulated
 * value itself (see _agg_average() below); most aggregators ignore it.
 * *aux_state is NULL until an aggregator allocates something and stores it
 * there; whatever gets stored is g_free()d once, unconditionally, when the
 * owning entry is destroyed (see _aggregate_entry_unref()), so it must be a
 * single, flat heap block with no further ownership of its own.
 */
typedef FilterXObject *(*FilterXAggregateFunc)(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state);


/* NOTE: all aggregators _MUST_ either return a reference counted object on
 * the heap or they must allocate a new object instance with the current
 * allocator.
 *
 * FIXME: this is easy to get wrong, do something about it...
 */
static FilterXObject *
_agg_replace(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  return filterx_object_dup(incoming);
}

static FilterXObject *
_agg_sum(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  if (existing)
    return filterx_object_add(existing, incoming);
  return filterx_object_dup(incoming);
}

static FilterXObject *
_agg_count(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  /* like SQL's COUNT(column): a null value is present, but doesn't count */
  if (filterx_object_extract_null(incoming))
    return existing ? filterx_object_dup(existing) : filterx_integer_new(0);

  if (!existing)
    return filterx_integer_new(1);

  gint64 n;
  if (!filterx_object_extract_integer(existing, &n))
    return NULL;
  return filterx_integer_new(n + 1);
}

static FilterXObject *
_agg_min(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  if (!existing)
    return filterx_object_dup(incoming);

  GenericNumber gn_existing, gn_incoming;
  if (!filterx_object_extract_generic_number(existing, &gn_existing) ||
      !filterx_object_extract_generic_number(incoming, &gn_incoming))
    return NULL;

  return filterx_object_dup(gn_compare(&gn_incoming, &gn_existing) < 0 ? incoming : existing);
}

static FilterXObject *
_agg_max(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  if (!existing)
    return filterx_object_dup(incoming);

  GenericNumber gn_existing, gn_incoming;
  if (!filterx_object_extract_generic_number(existing, &gn_existing) ||
      !filterx_object_extract_generic_number(incoming, &gn_incoming))
    return NULL;

  return filterx_object_dup(gn_compare(&gn_incoming, &gn_existing) > 0 ? incoming : existing);
}

/* per-(entry, field) aux state for _agg_average(): the displayed value is
 * only ever the average itself, which isn't enough on its own to fold in
 * the next incoming value without drifting, so the running sum and count
 * are kept alongside it instead. A flat POD struct, so a plain g_free() in
 * _aggregate_entry_unref() is enough to release it. */
typedef struct
{
  gdouble sum;
  gint64 count;
} FieldAverageState;

static FilterXObject *
_agg_average(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  GenericNumber gn_incoming;
  if (!filterx_object_extract_generic_number(incoming, &gn_incoming))
    return NULL;

  FieldAverageState *state = *aux_state;
  if (!state)
    {
      state = g_new0(FieldAverageState, 1);
      *aux_state = state;
    }

  state->sum += gn_as_double(&gn_incoming);
  state->count++;

  return filterx_double_new(state->sum / (gdouble) state->count);
}

/*
 * "_as_number" variants of the numeric aggregators: min/max/average/sum
 * above use filterx_object_extract_generic_number() directly, which is
 * strict -- it happily unmarshals a message-tied field (message_value) into
 * a number, but gives up on e.g. a plain FilterXObject string like "5"
 * (there's no message_value wrapper left to unmarshal). These variants
 * additionally parse such values, so a field coming from a literal/parsed
 * string still aggregates numerically instead of failing the merge.
 *
 * They also always normalize the stored value to a real number (even the
 * very first time a key is seen), unlike their plain counterparts, which
 * simply dup() whatever came in on the first message. Without that, e.g.
 * min_as_number would store the first incoming value's original string
 * form, then fail to parse it back out of `existing` on the very next
 * message.
 */
static gboolean
_coerce_to_generic_number(FilterXObject *obj, GenericNumber *gn)
{
  if (filterx_object_extract_generic_number(obj, gn))
    return TRUE;

  const gchar *str;
  if (filterx_object_extract_string_as_cstr(obj, &str))
    return parse_generic_number(str, gn);

  return FALSE;
}

static FilterXObject *
_generic_number_to_filterx_object(const GenericNumber *gn)
{
  if (gn->type == GN_INT64)
    return filterx_integer_new(gn_as_int64(gn));
  return filterx_double_new(gn_as_double(gn));
}

static FilterXObject *
_agg_sum_as_number(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  GenericNumber gn_incoming;
  if (!_coerce_to_generic_number(incoming, &gn_incoming))
    return NULL;

  if (!existing)
    return _generic_number_to_filterx_object(&gn_incoming);

  GenericNumber gn_existing;
  if (!_coerce_to_generic_number(existing, &gn_existing))
    return NULL;

  FilterXObject *existing_num = _generic_number_to_filterx_object(&gn_existing);
  FilterXObject *incoming_num = _generic_number_to_filterx_object(&gn_incoming);
  FilterXObject *result = filterx_object_add(existing_num, incoming_num);
  filterx_object_unref(existing_num);
  filterx_object_unref(incoming_num);
  return result;
}

static FilterXObject *
_agg_min_as_number(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  GenericNumber gn_incoming;
  if (!_coerce_to_generic_number(incoming, &gn_incoming))
    return NULL;

  if (!existing)
    return _generic_number_to_filterx_object(&gn_incoming);

  GenericNumber gn_existing;
  if (!_coerce_to_generic_number(existing, &gn_existing))
    return NULL;

  return _generic_number_to_filterx_object(gn_compare(&gn_incoming, &gn_existing) < 0 ? &gn_incoming : &gn_existing);
}

static FilterXObject *
_agg_max_as_number(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  GenericNumber gn_incoming;
  if (!_coerce_to_generic_number(incoming, &gn_incoming))
    return NULL;

  if (!existing)
    return _generic_number_to_filterx_object(&gn_incoming);

  GenericNumber gn_existing;
  if (!_coerce_to_generic_number(existing, &gn_existing))
    return NULL;

  return _generic_number_to_filterx_object(gn_compare(&gn_incoming, &gn_existing) > 0 ? &gn_incoming : &gn_existing);
}

static FilterXObject *
_agg_average_as_number(FilterXObject *existing, FilterXObject *incoming, gpointer *aux_state)
{
  GenericNumber gn_incoming;
  if (!_coerce_to_generic_number(incoming, &gn_incoming))
    return NULL;

  FieldAverageState *state = *aux_state;
  if (!state)
    {
      state = g_new0(FieldAverageState, 1);
      *aux_state = state;
    }

  state->sum += gn_as_double(&gn_incoming);
  state->count++;

  return filterx_double_new(state->sum / (gdouble) state->count);
}

static const struct
{
  const gchar *name;
  FilterXAggregateFunc aggregate;
} _named_aggregate_funcs[] =
{
  { "replace", _agg_replace },
  { "sum", _agg_sum },
  { "count", _agg_count },
  { "min", _agg_min },
  { "max", _agg_max },
  { "average", _agg_average },
  { "sum_as_number", _agg_sum_as_number },
  { "min_as_number", _agg_min_as_number },
  { "max_as_number", _agg_max_as_number },
  { "average_as_number", _agg_average_as_number },
};

static FilterXAggregateFunc
_lookup_named_aggregate_func(const gchar *name, gsize name_len)
{
  for (gsize i = 0; i < G_N_ELEMENTS(_named_aggregate_funcs); i++)
    {
      if (strlen(_named_aggregate_funcs[i].name) == name_len &&
          memcmp(_named_aggregate_funcs[i].name, name, name_len) == 0)
        return _named_aggregate_funcs[i].aggregate;
    }
  return NULL;
}

/* per-field override of the aggregator function, resolved once at init()
 * time from the "aggregators" argument (see _resolve_field_aggregators());
 * field_name is an owned copy since the literal dict it was extracted from
 * does not outlive init(). */
typedef struct
{
  gchar *field_name;
  gsize field_name_len;
  FilterXAggregateFunc aggregate;
} FieldAggregator;

static void
_field_aggregator_clear(FieldAggregator *fa)
{
  g_free(fa->field_name);
}

static void
_field_aggregators_free(GArray *field_aggregators)
{
  if (!field_aggregators)
    return;

  for (guint i = 0; i < field_aggregators->len; i++)
    _field_aggregator_clear(&g_array_index(field_aggregators, FieldAggregator, i));
  g_array_free(field_aggregators, TRUE);
}

typedef struct
{
  FilterXObject *target;
  GArray *field_aggregators;  /* of FieldAggregator, sorted by field_name; NULL means "no overrides, always use sum" */
  GPtrArray *field_aux_state; /* per-entry, indexed 1:1 with field_aggregators; NULL iff field_aggregators is NULL */
} AggregateMergeContext;

static gint
_compare_field_names(const gchar *a, gsize a_len, const gchar *b, gsize b_len)
{
  gsize min_len = MIN(a_len, b_len);
  gint cmp = min_len ? memcmp(a, b, min_len) : 0;
  if (cmp != 0)
    return cmp;
  if (a_len != b_len)
    return a_len < b_len ? -1 : 1;
  return 0;
}

static gint
_compare_field_aggregators(gconstpointer a, gconstpointer b)
{
  const FieldAggregator *fa = (const FieldAggregator *) a;
  const FieldAggregator *fb = (const FieldAggregator *) b;
  return _compare_field_names(fa->field_name, fa->field_name_len, fb->field_name, fb->field_name_len);
}

/* field_aggregators is small and sorted once at init() time (see
 * _resolve_field_aggregators()), so a binary search here beats both a
 * linear scan (fewer comparisons) and a GHashTable (no NUL-terminated key
 * required -- field_name arrives as a plain (ptr, len) pair from
 * filterx_object_extract_string_ref(), same as the compiled table).
 *
 * Returns NULL if @field_name has no override (caller defaults to sum); the
 * returned pointer is stable for the lifetime of @field_aggregators (sorted
 * once, never reallocated afterwards), so callers may keep using it (e.g.
 * to compute an index into a parallel per-entry array) after this returns. */
static FieldAggregator *
_lookup_field_aggregator(GArray *field_aggregators, const gchar *field_name, gsize field_name_len)
{
  if (!field_aggregators)
    return NULL;

  gint l = 0, h = (gint) field_aggregators->len - 1;
  while (l <= h)
    {
      gint m = (l + h) >> 1;
      FieldAggregator *fa = &g_array_index(field_aggregators, FieldAggregator, m);
      gint cmp = _compare_field_names(field_name, field_name_len, fa->field_name, fa->field_name_len);

      if (cmp == 0)
        return fa;
      else if (cmp < 0)
        h = m - 1;
      else
        l = m + 1;
    }
  return NULL;
}

static gboolean
_aggregate_elem(FilterXObject *key, FilterXObject *incoming, gpointer user_data)
{
  AggregateMergeContext *ctx = (AggregateMergeContext *) user_data;

  FilterXAggregateFunc aggregate = _agg_sum;
  gpointer *aux_state = NULL;
  const gchar *field_name;
  gsize field_name_len;
  if (filterx_object_extract_string_ref(key, &field_name, &field_name_len))
    {
      FieldAggregator *fa = _lookup_field_aggregator(ctx->field_aggregators, field_name, field_name_len);
      if (fa)
        {
          aggregate = fa->aggregate;
          gsize idx = fa - (FieldAggregator *) ctx->field_aggregators->data;
          aux_state = (gpointer *) &g_ptr_array_index(ctx->field_aux_state, idx);
        }
    }

  FilterXObject *existing = filterx_object_get_subscript(ctx->target, key);
  FilterXObject *new_value = aggregate(existing, incoming, aux_state);
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
_aggregate_merge(FilterXObject *target, FilterXObject *incoming_values, GArray *field_aggregators,
                 GPtrArray *field_aux_state)
{
  if (!filterx_object_is_type_or_ref(incoming_values, &FILTERX_TYPE_NAME(mapping)))
    return FALSE;

  AggregateMergeContext ctx = { .target = target, .field_aggregators = field_aggregators, .field_aux_state = field_aux_state };
  return filterx_object_iter(incoming_values, _aggregate_elem, &ctx);
}

#define FILTERX_FUNC_AGGREGATE_USAGE \
  "Usage: aggregate(key=expr, values=dict, timeout=seconds, [close=expr], [aggregators=dict])"

/* aggregate() returns a (status, values) tuple */
#define FILTERX_FUNC_AGGREGATE_STATUS_ABSORBED "absorbed"
#define FILTERX_FUNC_AGGREGATE_STATUS_CLOSED "closed"
#define FILTERX_FUNC_AGGREGATE_STATUS_TIMEOUT "timeout"

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
  FilterXEvalContinuation continuation;
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
  /* per-field aux state (e.g. running sum/count for "average"), indexed
   * 1:1 with the aggregate() call site's field_aggregators; NULL iff that
   * is NULL. Self-describing (GPtrArray tracks its own length), so freeing
   * it never needs to reach back into field_aggregators, which may already
   * be gone by the time an entry is torn down as part of _free() -- see
   * _aggregate_entry_unref(). */
  GPtrArray *field_aux_state;
  FilterXEvalContext *saved_context;
  MlBatchedTimer timer;
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

static void
_shared_state_cancel_timers(AggregateSharedState *shared)
{
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, shared->entries);
  while (g_hash_table_iter_next(&iter, &key, &value))
    {
      AggregateEntry *entry = (AggregateEntry *) value;
      ml_batched_timer_unregister(&entry->timer);
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
      filterx_eval_context_free_dup(entry->saved_context);
      filterx_object_unref(entry->tuple_key);
      filterx_object_unref(entry->values);
      if (entry->field_aux_state)
        g_ptr_array_free(entry->field_aux_state, TRUE);
      ml_batched_timer_free(&entry->timer);
      _shared_state_unref(entry->shared);
      g_free(entry);
    }
}

/* aggregation logic, operates on shared state */

static void
_replay_and_forward(AggregateSharedState *shared, AggregateEntry *entry)
{
  FilterXObject *values = filterx_ref_float(filterx_object_copy(entry->values));
  FilterXObject *result = _wrap_result(FILTERX_FUNC_AGGREGATE_STATUS_TIMEOUT, values);
  log_filterx_pipe_resume_and_forward(shared->continuation.owner_pipe, entry->saved_context, &shared->continuation,
                                      result);
  filterx_object_unref(result);
}

static void
_expire_entry(AggregateSharedState *shared, AggregateEntry *entry)
{
  g_mutex_lock(&shared->lock);
  gboolean already_closed = entry->closed;
  entry->closed = TRUE;
  g_mutex_unlock(&shared->lock);

  if (already_closed)
    return;

  if (shared->continuation.statement_expr)
    _replay_and_forward(shared, entry);

  g_mutex_lock(&shared->lock);
  g_hash_table_remove(shared->entries, entry->tuple_key);
  g_mutex_unlock(&shared->lock);
}

static void
_close_entry(AggregateSharedState *shared, AggregateEntry *entry)
{
  entry->closed = TRUE;
  ml_batched_timer_cancel(&entry->timer);
  g_hash_table_remove(shared->entries, entry->tuple_key);
}

static void
_on_aggregate_entry_expired(gpointer cookie)
{
  AggregateEntry *entry = (AggregateEntry *) cookie;

  main_loop_assert_main_thread();
  _expire_entry(entry->shared, entry);
}

static AggregateEntry *
_new_entry(AggregateSharedState *shared, FilterXObject *tuple_key, GArray *field_aggregators)
{
  AggregateEntry *entry = g_new0(AggregateEntry, 1);

  g_atomic_counter_set(&entry->ref_cnt, 1);
  entry->shared = _shared_state_ref(shared);
  entry->tuple_key = filterx_object_ref(tuple_key);
  entry->values = filterx_dict_new();
  filterx_object_cow_prepare(&entry->values);

  if (field_aggregators)
    {
      entry->field_aux_state = g_ptr_array_new_with_free_func(g_free);
      g_ptr_array_set_size(entry->field_aux_state, field_aggregators->len);
    }

  ml_batched_timer_init(&entry->timer);
  entry->timer.cookie = entry;
  entry->timer.handler = _on_aggregate_entry_expired;
  entry->timer.ref_cookie = (void *(*)(void *)) _aggregate_entry_ref;
  entry->timer.unref_cookie = (void (*)(void *)) _aggregate_entry_unref;

  g_hash_table_insert(shared->entries, filterx_object_ref(tuple_key), entry);
  return entry;
}

static void
_arm_new_entry(AggregateSharedState *shared, AggregateEntry *entry, gint64 timeout_seconds)
{
  entry->saved_context = filterx_eval_context_dup(filterx_eval_get_context());
  ml_batched_timer_postpone(&entry->timer, timeout_seconds);
}

static FilterXObject *
_aggregate(AggregateSharedState *shared, FilterXObject *tuple_key, FilterXObject *values,
           gboolean close,
           gint64 timeout_seconds,
           GArray *field_aggregators)
{
  AggregateEntry *entry = g_hash_table_lookup(shared->entries, tuple_key);
  gboolean is_new_entry = !entry;
  if (!entry)
    entry = _new_entry(shared, tuple_key, field_aggregators);

  if (!_aggregate_merge(entry->values, values, field_aggregators, entry->field_aux_state))
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

  if (is_new_entry && timeout_seconds > 0 && shared->continuation.statement_expr)
    _arm_new_entry(shared, entry, timeout_seconds);

  return _wrap_result(FILTERX_FUNC_AGGREGATE_STATUS_ABSORBED, merged_values);
}

typedef struct FilterXFunctionAggregate_
{
  FilterXFunction super;
  FilterXExpr *key_expr;
  FilterXExpr *values_expr;
  FilterXExpr *close_expr;
  FilterXExpr *aggregators_expr;
  GArray *field_aggregators;
  gint64 timeout_seconds;
  AggregateSharedState *shared;
} FilterXFunctionAggregate;

typedef struct
{
  GArray *field_aggregators;
  gchar *error;
} FieldAggregatorParseContext;

static gboolean
_parse_field_aggregator(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FieldAggregatorParseContext *ctx = (FieldAggregatorParseContext *) user_data;

  const gchar *field_name;
  gsize field_name_len;
  if (!filterx_object_extract_string_ref(key, &field_name, &field_name_len))
    {
      ctx->error = g_strdup("aggregators argument keys must be strings");
      return FALSE;
    }

  const gchar *func_name;
  gsize func_name_len;
  if (!filterx_object_extract_string_ref(value, &func_name, &func_name_len))
    {
      ctx->error = g_strdup_printf("aggregators[\"%.*s\"] must be a string", (gint) field_name_len, field_name);
      return FALSE;
    }

  FilterXAggregateFunc aggregate = _lookup_named_aggregate_func(func_name, func_name_len);
  if (!aggregate)
    {
      ctx->error = g_strdup_printf("aggregators[\"%.*s\"] names an unknown aggregator function: \"%.*s\"",
                                   (gint) field_name_len, field_name, (gint) func_name_len, func_name);
      return FALSE;
    }

  FieldAggregator fa =
  {
    .field_name = g_strndup(field_name, field_name_len),
    .field_name_len = field_name_len,
    .aggregate = aggregate,
  };
  g_array_append_val(ctx->field_aggregators, fa);
  return TRUE;
}

static gboolean
_resolve_field_aggregators(FilterXFunctionAggregate *self)
{
  if (!self->aggregators_expr)
    return TRUE;

  if (!filterx_expr_is_literal(self->aggregators_expr))
    {
      filterx_eval_push_error_static_info("aggregate(): failed to resolve aggregators argument",
                                          "aggregators argument must be a literal dict. "
                                          FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }

  FilterXObject *aggregators = filterx_literal_get_value(self->aggregators_expr);
  if (!filterx_object_is_type_or_ref(aggregators, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_eval_push_error_static_info("aggregate(): failed to resolve aggregators argument",
                                          "aggregators argument must be a dict. " FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }

  self->field_aggregators = g_array_new(FALSE, FALSE, sizeof(FieldAggregator));

  FieldAggregatorParseContext ctx = { .field_aggregators = self->field_aggregators, .error = NULL };
  if (!filterx_object_iter(aggregators, _parse_field_aggregator, &ctx))
    {
      filterx_eval_push_error_info_printf("aggregate(): failed to resolve aggregators argument",
                                          "%s. " FILTERX_FUNC_AGGREGATE_USAGE,
                                          ctx.error ? ctx.error : "invalid aggregators argument");
      g_free(ctx.error);
      return FALSE;
    }

  /* dict keys are unique, so no duplicates to worry about here -- sort
   * once so _lookup_field_aggregator() can binary search it. */
  g_array_sort(self->field_aggregators, _compare_field_aggregators);

  return TRUE;
}

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
  FilterXObject *resumed = filterx_eval_take_resume_value();
  if (resumed)
    {
      /* resume on continunation */
      return resumed;
    }

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
  result = _aggregate(self->shared, tuple_key, values, close, self->timeout_seconds, self->field_aggregators);
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

  if (!_resolve_field_aggregators(self))
    return FALSE;

  FilterXEvalContinuation *continuation = filterx_eval_get_continuation();
  if (continuation)
    self->shared->continuation = *continuation;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_aggregate_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;

  _shared_state_cancel_timers(self->shared);
  g_hash_table_remove_all(self->shared->entries);

  filterx_function_deinit_method(&self->super, cfg);
}

static gboolean
_aggregate_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;

  FilterXExpr **exprs[] = { &self->key_expr, &self->values_expr, &self->close_expr, &self->aggregators_expr };

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

  gboolean exists, eval_error;
  self->timeout_seconds = filterx_function_args_get_named_literal_integer(args, "timeout", &exists, &eval_error);
  if (!exists)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "timeout argument is required. " FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }
  if (eval_error)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "timeout argument must be a literal integer. " FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }
  if (self->timeout_seconds <= 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "timeout argument must be a positive number of seconds. " FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }

  self->close_expr = filterx_function_args_get_named_expr(args, "close");
  self->aggregators_expr = filterx_function_args_get_named_expr(args, "aggregators");

  return TRUE;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;

  filterx_expr_unref(self->key_expr);
  filterx_expr_unref(self->values_expr);
  filterx_expr_unref(self->close_expr);
  filterx_expr_unref(self->aggregators_expr);
  _field_aggregators_free(self->field_aggregators);
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

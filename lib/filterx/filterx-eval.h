/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#ifndef FILTERX_EVAL_H_INCLUDED
#define FILTERX_EVAL_H_INCLUDED

#include "filterx/filterx-scope.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-error.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-allocator.h"
#include "filterx/filterx-env.h"
#include "template/eval.h"

#define FILTERX_CONTEXT_ERROR_STACK_SIZE (32)
#define FILTERX_EVAL_ERROR_IDX_FMT_SIZE (8)

typedef enum _FilterXEvalResult
{
  FXE_SUCCESS,
  FXE_FAILURE,
  FXE_DROP,
} FilterXEvalResult;


typedef enum _FilterXEvalControl
{
  /* default, not set value, not to be confused with the unset() function */
  FXC_UNSET,
  /* exit from the current filterx {} block with success (matched), drop the message */
  FXC_DROP,
  /* exit from the current filterx {} block with success (matched), accept the message */
  FXC_DONE,
  /* exit from the current compound expression, continue execution with the next statement in the same filterx {} block */
  FXC_BREAK,
} FilterXEvalControl;

typedef struct _FilterXFailureInfo
{
  FilterXObject *meta;
  FilterXError errors[FILTERX_CONTEXT_ERROR_STACK_SIZE];
  gint error_count;
} FilterXFailureInfo;

typedef struct _FilterXEvalContext FilterXEvalContext;
struct _FilterXEvalContext
{
  LogMessage *msg;
  FilterXScope *scope;
  FilterXError errors[FILTERX_CONTEXT_ERROR_STACK_SIZE];
  gint error_count;
  FilterXObject *current_frame_meta;
  LogTemplateEvalOptions template_eval_options;
  GPtrArray *weak_refs;
  FilterXAllocator *allocator;
  FilterXAllocatorPosition allocator_position;
  FilterXEvalControl eval_control_modifier;
  FilterXEvalContext *previous_context;

  guint8 failure_info_collect_falsy:1, allocations_shared:1;
  GArray *failure_info;
  gint weak_refs_offset;
  FilterXEnvironment *env;
};

FilterXEvalContext *filterx_eval_get_context(void);
FilterXScope *filterx_eval_get_scope(void);
void filterx_eval_update_error_location_from_expr(FilterXExpr *expr);
void filterx_eval_push_error(const gchar *message, FilterXObject *object);
void filterx_eval_push_falsy_error(const gchar *message, FilterXObject *object);
void filterx_eval_push_error_static_info(const gchar *message, const gchar *info);
void filterx_eval_push_error_info_printf(const gchar *message, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);
void filterx_eval_set_context(FilterXEvalContext *context);
FilterXEvalResult filterx_eval_exec(FilterXEvalContext *context, FilterXExpr *expr, FilterXJITExecFunc jit_exec);
const gchar *filterx_eval_get_last_error(void);
const gchar *filterx_eval_get_error(gint index);
gint filterx_eval_get_error_count(void);
EVTTAG *filterx_eval_format_error_tag(gint index);
EVTTAG *filterx_eval_format_error_location_tag(gint index);
void filterx_eval_clear_errors(void);
EVTTAG *filterx_eval_format_error_index_tag(gint index, gchar *buf);
EVTTAG *filterx_format_eval_result(FilterXEvalResult result);
void filterx_eval_dump_errors(const gchar *message);

void filterx_eval_begin_context(FilterXEvalContext *context, FilterXEvalContext *previous_context,
                                FilterXScope *scope_storage, LogMessage *msg);
void filterx_eval_end_context(FilterXEvalContext *context);
void filterx_eval_begin_restricted_context(FilterXEvalContext *context, FilterXEnvironment *env);
void filterx_eval_end_restricted_context(FilterXEvalContext *context);

void filterx_eval_begin_compile(FilterXEvalContext *context, GlobalConfig *cfg);
void filterx_eval_end_compile(FilterXEvalContext *context);
void filterx_eval_freeze_object(FilterXObject **object);

FilterXEvalControl filterx_eval_get_control_modifier(FilterXEvalContext *context);
void filterx_eval_set_control_modifier(FilterXEvalContext *context, FilterXEvalControl modifier);

void filterx_eval_enable_failure_info(FilterXEvalContext *context, gboolean collect_falsy);
void filterx_eval_clear_failure_info(FilterXEvalContext *context);
GArray *filterx_eval_get_failure_info(FilterXEvalContext *context);

static inline void
filterx_eval_set_current_frame_meta(FilterXEvalContext *context, FilterXObject *meta)
{
  filterx_object_unref(context->current_frame_meta);
  context->current_frame_meta = filterx_object_ref(meta);
}

static inline void
filterx_eval_sync_message(FilterXEvalContext *context, LogMessage **pmsg, const LogPathOptions *path_options)
{
  if (!context)
    return;

  if (!filterx_scope_is_dirty(context->scope))
    return;

  filterx_scope_sync(context->scope, pmsg, path_options);
}

static inline void
filterx_eval_prepare_for_fork(FilterXEvalContext *context, LogMessage **pmsg, const LogPathOptions *path_options)
{
  filterx_eval_sync_message(context, pmsg, path_options);
  if (context)
    filterx_scope_mark_fork_point(context->scope);
}

/*
 * This is not a real weakref implementation as we will never get rid off
 * weak references until the very end of a scope.  If this wasn't the case
 * we would have to:
 *    1) run a proper GC
 *    2) notify weak references once the object is detroyed
 *
 * None of that exists now and I doubt ever will (but never say never).
 * Right now a weak ref is destroyed as a part of the scope finalization
 * process at which point circular references will be broken so the rest can
 * go too.
 */
static inline void
filterx_eval_store_weak_ref(FilterXObject *object)
{
  /* Preserved objects do not need weak refs. */
  if (object && (filterx_object_is_preserved(object)))
    return;

  if (object && !object->weak_referenced)
    {
      FilterXEvalContext *context = filterx_eval_get_context();
      /* avoid putting object to the list multiple times */
      object->weak_referenced = TRUE;
      g_assert(context->weak_refs);
      g_ptr_array_add(context->weak_refs, filterx_object_ref(object));
    }
}

#define FILTERX_EVAL_BEGIN_CONTEXT(eval_context, previous_context, msg, scope_var_layout) \
  do { \
    FilterXEvalContext *_prev_ctx = (FilterXEvalContext *)(previous_context); \
    FilterXScope *fx_scope = NULL; \
    gboolean local_scope = FALSE; \
    \
    if (!fx_scope) \
      { \
        gsize alloc_size = filterx_scope_get_alloc_size(scope_var_layout); \
        fx_scope = (FilterXScope *) g_alloca(alloc_size); \
        filterx_scope_init_instance(fx_scope, alloc_size, _prev_ctx ? _prev_ctx->scope : NULL, scope_var_layout); \
        local_scope = TRUE; \
      } \
    filterx_eval_begin_context(&eval_context, _prev_ctx, fx_scope, msg); \
    do


#define FILTERX_EVAL_END_CONTEXT(eval_context) \
    while(0); \
    \
    if (local_scope) \
      filterx_scope_clear(fx_scope); \
    filterx_eval_end_context(&eval_context); \
  } while(0)

static inline gboolean
filterx_eval_context_allocations_are_shared(FilterXEvalContext *context)
{
  if (!context)
    return FALSE;
  return context->allocations_shared;
}

static inline FilterXObject *
filterx_eval_malloc_object(gsize object_size, gsize alloc_size)
{
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXObject *result;

  if (!context || !context->allocator || !filterx_allocator_alloc_size_supported(context->allocator, alloc_size))
    {
      result = (FilterXObject *) g_malloc(alloc_size);
      memset(result, 0, object_size);
    }
  else
    {
      result = (FilterXObject *) filterx_allocator_malloc(context->allocator, alloc_size, object_size);
      result->allocator_used = TRUE;
    }
  result->early_allocation = filterx_eval_context_allocations_are_shared(context);

  return result;
}

static inline void
filterx_eval_switch_allocator(FilterXAllocator *new_allocator, gpointer *saved_state)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  *saved_state = context->allocator;
  context->allocator = new_allocator;
}

static inline void
filterx_eval_disable_allocator(gpointer *saved_state)
{
  filterx_eval_switch_allocator(NULL, saved_state);
}

static inline void
filterx_eval_restore_allocator(gpointer *saved_state)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  context->allocator = (FilterXAllocator *) *saved_state;
  *saved_state = NULL;
}

/*
 * Retaining an object:
 *
 * Sometimes we need to retain an object for longer than what its initial
 * allocation can warrant (e.g.  allocated from a thread specific allocator
 * and we need to keep it until aggregation finishes).  An important
 * constraint is that retained objects only allow read only access.
 *
 * The FX_RETAIN_* enums determine how long we want to make sure an object
 * remains accessible.  A return value can refer to the same object (in case
 * the current allocation suffices), or it can duplicate the object, in
 * which case the caller will have its own copy.
 *
 * In both cases, only read only access is permitted, otherwise an object
 * could potentially be accessed from multiple threads.
 *
 * An object instance can be allocated in one of the following ways:
 *
 * Shared access (multiple threads use the same object):
 * -----------------------------------------------------
 *   1) allocated early, at configuration load time and then frozen (filterx_object_is_hibernated())
 *
 *   2) allocated during runtime, frozen for a longer period, but then freed
 *      asynchronously (e.g.  cache_json_file()) (filterx_object_is_frozen())
 *
 * Exclusive access (a single thread has access):
 * ----------------------------------------------
 *   3) allocated during runtime for pipeline access, using a pipeline
 *      allocator that frees objects en-masse, the reference count exists
 *      (for now) but does not drive the freeing of the object.  Once the
 *      message is delivered, all objects are freed.
 *      (filterx_object_is_refcounted() && object->allocator_used)
 *
 *   4) allocated during runtime for a single thread access, not using a
 *      pipeline allocator, e.g.  the reference count drives the freeing of the
 *      object. (filterx_object_is_refcounted() && !object->allocator_used)
 */

typedef enum
{
  /* retain this object at least up to the last delivery of the current
   * message */
  FX_RETAIN_UNTIL_FINAL_DELIVERY,
  /* retain this object at least up to the next configuration reload */
  FX_RETAIN_UNTIL_RELOAD,

  /* retain this object independently from configurations, e.g.  until its
   * reference count is larger than zero, even in face of configuration
   * reloads */
  FX_RETAIN_DECOUPLE_CONFIG,
} FilterXEvalRetainGoal;

static inline gboolean
filterx_eval_retain_dup_needed(FilterXObject *object, FilterXEvalRetainGoal goal)
{
  if (!object)
    return FALSE;

  /* at any goals, we need to get our of the allocator's purview */
  if (object->allocator_used)
    return TRUE;

  switch (goal)
    {
    case FX_RETAIN_UNTIL_FINAL_DELIVERY:
      return FALSE;
    case FX_RETAIN_UNTIL_RELOAD:
      if (object->early_allocation)
        {
          /* early allocated objects, these are always preserved and will be good until reload */
#if SYSLOG_NG_ENABLE_DEBUG
          g_assert(filterx_object_is_preserved(object));
#endif
          return FALSE;
        }
      /* refcounted and hibnerated objects don't need duplication */
      if (filterx_object_is_refcounted(object) ||
          filterx_object_is_hibernated(object))
        return FALSE;
      /* the remaining case is stashed objects, which are not safe to keep
       * around, so let's duplicate those */

#if SYSLOG_NG_ENABLE_DEBUG
      g_assert(filterx_object_is_frozen(object));
#endif
      return TRUE;
    case FX_RETAIN_DECOUPLE_CONFIG:
      /* we want a proper reference counted object, allocated on the heap,
       * preserved objects are not good enough as we want them to survive a
       * reload */
      if (!filterx_object_is_preserved(object))
        return FALSE;
      return TRUE;
    default:
      g_assert_not_reached();
    }
}

/* unplug this object from the current context, and guarantee it remains
 * available past the end of the scope, at least until the returned
 * reference is dropped using filterx_object_unref(). */
static inline FilterXObject *
filterx_eval_retain_dup(FilterXObject *object, FilterXEvalRetainGoal goal)
{
  if (!filterx_eval_retain_dup_needed(object, goal))
    return filterx_object_ref(object);
  else
    return filterx_object_dup(object);
}

static inline void
filterx_eval_retain_object(FilterXObject **pobject, FilterXEvalRetainGoal goal)
{
  FilterXObject *object = *pobject;
  gpointer allocator_state;

  filterx_eval_disable_allocator(&allocator_state);
  *pobject = filterx_eval_retain_dup(object, goal);
  filterx_eval_restore_allocator(&allocator_state);
  filterx_object_unref(object);
}


void filterx_eval_global_init(void);
void filterx_eval_global_deinit(void);

#endif

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
#include "filterx/filterx-stashed-object.h"
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
  /* exit from the innermost enclosing switch, continue execution with the statement after it,
   * only valid inside a switch, enforced at init */
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
  const LogPathOptions *path_options;
  FilterXScope *scope;
  FilterXError errors[FILTERX_CONTEXT_ERROR_STACK_SIZE];
  gint error_count;
  FilterXObject *current_frame_meta;
  GPtrArray *weak_refs;
  FilterXAllocator *allocator;
  /* an arena owned by this context (a snapshot, see
   * filterx_eval_context_dup()), released along with it */
  FilterXAllocator *private_allocator;
  FilterXAllocatorPosition allocator_position;
  FilterXEvalControl eval_control_modifier;
  FilterXEvalContext *previous_context;

  guint8 failure_info_collect_falsy:1, allocations_shared:1;
  GArray *failure_info;
  gint weak_refs_offset;
  FilterXEnvironment *env;
  /* borrowed: the evaluation takes no reference to it */
  FilterXStashedObject **config_stash;
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
                                FilterXStashedObject **config_stash,
                                FilterXScope *scope_storage, LogMessage *msg);
void filterx_eval_end_context(FilterXEvalContext *context);
LogMessage *filterx_eval_make_message_writable(FilterXEvalContext *context);
void filterx_eval_begin_restricted_context(FilterXEvalContext *context, FilterXEnvironment *env);
void filterx_eval_end_restricted_context(FilterXEvalContext *context);

FilterXStashedObject **filterx_eval_get_config_stash(GlobalConfig *cfg);
void filterx_eval_begin_compile(FilterXEvalContext *context, GlobalConfig *cfg);
void filterx_eval_end_compile(FilterXEvalContext *context);
void filterx_eval_freeze_object(FilterXObject **object);

/*
 * filterx_eval_context_dup():
 *
 * Takes a full, self contained snapshot of @context: its scope (and the
 * scope's entire parent chain) with all of its variables/values, and
 * current_frame_meta.  Every value is copied into the snapshot, except a
 * preserved one, which is shared: a hibernated one lasts as long as the
 * process, a frozen one as long as the stash references the snapshot
 * takes.
 *
 * The result is a heap allocated, standalone FilterXEvalContext with no
 * previous_context and an arena of its own.  So it can be kept around
 * and used to resume evaluation later, potentially on a different
 * thread, long after @context (and the thread specific allocator/stack
 * storage it may be using) has gone away.
 *
 * @context must be the context that is currently active in this thread
 * (i.e.  filterx_eval_get_context() == context): it is set back as the
 * active context on return.
 *
 * The returned context must eventually be released with
 * filterx_eval_context_free_dup().
 */
FilterXEvalContext *filterx_eval_context_dup(FilterXEvalContext *context);
void filterx_eval_context_free_dup(FilterXEvalContext *context);

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

#define FILTERX_EVAL_BEGIN_CONTEXT(eval_context, previous_context, config_stash, msg, scope_var_layout) \
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
    filterx_eval_begin_context(&eval_context, _prev_ctx, config_stash, fx_scope, msg); \
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
      result->allocator_id = context->allocator->id;
    }
  result->early_allocation = filterx_eval_context_allocations_are_shared(context);

  return result;
}

static inline void
filterx_eval_switch_allocator(FilterXAllocator *new_allocator, gpointer *saved_state)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  /* no context, no allocator: nothing to switch away from */
  if (!context)
    {
      *saved_state = NULL;
      return;
    }
  *saved_state = context->allocator;
  context->allocator = new_allocator;
}

/* the id an object of @alloc_size allocated right now would carry:
 * FILTERX_ALLOCATOR_ID_HEAP if there is no allocator to allocate from, or if
 * the allocation is too large for it, see filterx_eval_malloc_object() */
static inline guint16
filterx_eval_current_allocator_id(gsize alloc_size)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (!context || !context->allocator || !filterx_allocator_alloc_size_supported(context->allocator, alloc_size))
    return FILTERX_ALLOCATOR_ID_HEAP;
  return context->allocator->id;
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

  if (context)
    context->allocator = (FilterXAllocator *) *saved_state;
  *saved_state = NULL;
}

/* unplug this object from the current context, and guarantee it remains
 * available past the end of the scope, at least until the returned
 * reference is dropped using filterx_object_unref().  The copy is made
 * with the allocator in effect, so the guarantee is as good as that: a
 * caller switches the allocator first, off for an object that has to
 * outlive the arenas, see filterx_eval_disable_allocator(). */
static inline FilterXObject *
filterx_eval_retain_dup(FilterXObject *object)
{
  if (!object || filterx_object_is_preserved(object))
    return filterx_object_ref(object);

  if (filterx_object_is_allocator_resident(object))
    return filterx_object_dup(object);

#if SYSLOG_NG_ENABLE_DEBUG

  /* filterx_object_ref() clones a stack object: a caller handing one here
   * skipped the reference it owes */
  g_assert(("retaining a stack allocated object; take a reference first" &&
            !filterx_object_is_stack_allocated(object)));
#endif

  return filterx_object_ref(object);
}

static inline void
filterx_eval_retain_object(FilterXObject **pobject)
{
  FilterXObject *object = *pobject;

  *pobject = filterx_eval_retain_dup(object);
  filterx_object_unref(object);
}

void filterx_eval_global_init(void);
void filterx_eval_global_deinit(void);

#endif

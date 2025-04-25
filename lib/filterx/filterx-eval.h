/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include "template/eval.h"


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
  FilterXError error;
} FilterXFailureInfo;

typedef struct _FilterXEvalContext FilterXEvalContext;
struct _FilterXEvalContext
{
  LogMessage *msg;
  FilterXScope *scope;
  FilterXError error;
  FilterXObject *current_frame_meta;
  LogTemplateEvalOptions template_eval_options;
  GPtrArray *weak_refs;
  FilterXEvalControl eval_control_modifier;
  FilterXEvalContext *previous_context;

  guint8 failure_info_collect_falsy:1;
  GArray *failure_info;
};


void filterx_eval_set_context(FilterXEvalContext *context);
FilterXEvalContext *filterx_eval_get_context(void);
FilterXScope *filterx_eval_get_scope(void);
FilterXEvalResult filterx_eval_exec(FilterXEvalContext *context, FilterXExpr *expr);
EVTTAG *filterx_format_eval_result(FilterXEvalResult result);

void filterx_eval_begin_context(FilterXEvalContext *context, FilterXEvalContext *previous_context,
                                FilterXScope *scope_storage, LogMessage *msg);
void filterx_eval_end_context(FilterXEvalContext *context);
void filterx_eval_begin_compile(FilterXEvalContext *context, GlobalConfig *cfg);
void filterx_eval_end_compile(FilterXEvalContext *context);

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
filterx_eval_context_push_error(FilterXEvalContext *context,
                                const gchar *message,
                                FilterXExpr *expr,
                                FilterXObject *object)
{
  filterx_error_clear(&context->error);
  filterx_error_set_values(&context->error, message, expr, object);
}

static inline void
filterx_eval_push_error(const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (context)
    filterx_eval_context_push_error(message, expr, object);
}

static inline void
filterx_eval_context_push_error_info(FilterXEvalContext *context,
                                     const gchar *message,
                                     FilterXExpr *expr,
                                     gchar *info,
                                     gboolean free_info)
{
  filterx_error_clear(&context->error);
  filterx_error_set_values(&context->error, message, expr, NULL);
  filterx_error_set_info(&context->error, info, free_info);
}

/* takes ownership of info */
static inline void
filterx_eval_push_error_info(const gchar *message, FilterXExpr *expr, gchar *info, gboolean free_info)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (context)
    {
      filterx_eval_context_push_error_info(context, message, expr, info, free_info);
    }
  else
    {
      if (free_info)
        g_free(info);
    }
}

static inline void
filterx_eval_context_push_falsy_error(FilterXEvalContext *context,
                                      const gchar *message,
                                      FilterXExpr *expr,
                                      FilterXObject *object)
{
  filterx_error_clear(&context->error);
  filterx_falsy_error_set_values(&context->error, message, expr, object);
}

static inline void
filterx_eval_push_falsy_error(const gchar *message, FilterXExpr *expr, FilterXObject *object)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  if (context)
    filterx_eval_context_push_falsy_error(context, message, expr, object);
}


static inline void
filterx_eval_clear_errors(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  filterx_error_clear(&context->error);
}

static inline const gchar *
filterx_eval_get_last_error(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return filterx_error_format(&context->error);
}

static inline EVTTAG *
filterx_eval_format_last_error_tag(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return filterx_error_format_tag(&context->error);
}

static inline EVTTAG *
filterx_eval_format_last_error_location_tag(void)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  return filterx_error_format_location_tag(&context->error);
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
    filterx_scope_write_protect(context->scope);
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
  if (object && filterx_object_is_preserved(object))
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

#define FILTERX_EVAL_BEGIN_CONTEXT(eval_context, previous_context) \
  do { \
    FilterXScope *scope = NULL; \
    gboolean local_scope = FALSE; \
    \
    if (previous_context) \
      scope = filterx_scope_reuse(previous_context->scope); \
    \
    if (!scope) \
      { \
        gsize alloc_size = filterx_scope_get_alloc_size(); \
        scope = g_alloca(alloc_size); \
        filterx_scope_init_instance(scope, alloc_size, previous_context ? previous_context->scope : NULL); \
        local_scope = TRUE; \
      } \
    filterx_eval_begin_context(&eval_context, previous_context, scope, msg); \
    do


#define FILTERX_EVAL_END_CONTEXT(eval_context) \
    while(0); \
    filterx_eval_end_context(&eval_context); \
    if (local_scope) \
      filterx_scope_clear(scope); \
  } while(0)

#endif

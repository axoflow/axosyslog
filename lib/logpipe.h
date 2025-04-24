/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#ifndef LOGPIPE_H_INCLUDED
#define LOGPIPE_H_INCLUDED

#include "logpath.h"
#include "logmsg/logmsg.h"
#include "cfg.h"
#include "atomic.h"
#include "messages.h"

/* notify code values */
#define NC_CLOSE       1
#define NC_READ_ERROR  2
#define NC_WRITE_ERROR 3
#define NC_FILE_MOVED  4
#define NC_FILE_EOF    5
#define NC_REOPEN_REQUIRED 6
#define NC_FILE_DELETED 7

/* indicates that the LogPipe was initialized */
#define PIF_INITIALIZED       0x0001
/* indicates that this LogPipe got cloned into the tree already */
#define PIF_INLINED           0x0002

/* this pipe is a source for messages, it is not meant to be used to
 * forward messages, syslog-ng will only use these pipes for the
 * left-hand side of the processing graph, e.g. no other pipes may be
 * sending messages to these pipes and these are expected to generate
 * messages "automatically". */

#define PIF_SOURCE            0x0004

/* log statement flags that are copied to the head of a branch */
#define PIF_BRANCH_FINAL      0x0008
#define PIF_BRANCH_FALLBACK   0x0010
#define PIF_BRANCH_PROPERTIES (PIF_BRANCH_FINAL + PIF_BRANCH_FALLBACK)

/* branch starting with this pipe wants hard flow control */
#define PIF_HARD_FLOW_CONTROL 0x0020

/* LogPipe right after the filter in an "if (filter)" expression */
#define PIF_CONDITIONAL_MIDPOINT  0x0040

/* LogPipe as the joining element of a junction */
#define PIF_JUNCTION_END          0x0080

/* node created directly by the user */
#define PIF_CONFIG_RELATED    0x0100

/* sync filterx state to message in right before calling queue() */
#define PIF_SYNC_FILTERX_TO_MSG      0x0200

/* private flags range, to be used by other LogPipe instances for their own purposes */

#define PIF_PRIVATE(x)       ((x) << 16)

typedef struct _LogPipeOptions LogPipeOptions;

struct _LogPipeOptions
{
  gboolean internal;
};

struct _LogPipe
{
  GAtomicCounter ref_cnt;
  gint32 flags;

  void (*queue)(LogPipe *self, LogMessage *msg, const LogPathOptions *path_options);

  GlobalConfig *cfg;
  LogExprNode *expr_node;
  LogPipe *pipe_next;
  StatsCounterItem *discarded_messages;
  const gchar *persist_name;
  gchar *plugin_name;
  LogPipeOptions options;

  gboolean (*pre_init)(LogPipe *self);
  gboolean (*init)(LogPipe *self);
  gboolean (*deinit)(LogPipe *self);
  void (*post_deinit)(LogPipe *self);

  gboolean (*pre_config_init)(LogPipe *self);
  /* this event function is used to perform necessary operation, such as
   * starting worker thread, and etc. therefore, syslog-ng will terminate if
   * return value is false.
   */
  gboolean (*post_config_init)(LogPipe *self);

  const gchar *(*generate_persist_name)(const LogPipe *self);
  void (*walk)(LogPipe *self, LogPathWalkFunc func, gpointer user_data);

  /* clone this pipe when used in multiple locations in the processing
   * pipe-line. If it contains state, it should behave as if it was
   * the same instance, otherwise it can be a copy.
   */
  LogPipe *(*clone)(LogPipe *self);

  void (*free_fn)(LogPipe *self);
  void (*notify)(LogPipe *self, gint notify_code, gpointer user_data);
  GList *info;
};

/*
   cpu-cache optimization: queue method should be as close as possible to flags.

   Rationale: the pointer pointing to this LogPipe instance is
   resolved right before calling queue and the colocation of flags and
   the queue pointer ensures that they are on the same cache line. The
   queue method is probably the single most often called virtual method
 */
G_STATIC_ASSERT(G_STRUCT_OFFSET(LogPipe, queue) - G_STRUCT_OFFSET(LogPipe, flags) <= 4);

extern gboolean (*pipe_single_step_hook)(LogPipe *pipe, LogMessage *msg, const LogPathOptions *path_options);

LogPipe *log_pipe_ref(LogPipe *self);
gboolean log_pipe_unref(LogPipe *self);
LogPipe *log_pipe_new(GlobalConfig *cfg);
gboolean log_pipe_pre_config_init_method(LogPipe *self);
gboolean log_pipe_post_config_init_method(LogPipe *self);
void log_pipe_walk_method(LogPipe *self, LogPathWalkFunc func, gpointer user_data);
void log_pipe_init_instance(LogPipe *self, GlobalConfig *cfg);
void log_pipe_clone_method(LogPipe *dst, const LogPipe *src);
void log_pipe_forward_notify(LogPipe *self, gint notify_code, gpointer user_data);
EVTTAG *log_pipe_location_tag(LogPipe *pipe);
void log_pipe_attach_expr_node(LogPipe *self, LogExprNode *expr_node);
void log_pipe_detach_expr_node(LogPipe *self);

static inline GlobalConfig *
log_pipe_get_config(LogPipe *s)
{
  g_assert(s->cfg);
  return s->cfg;
}

static inline void
log_pipe_set_config(LogPipe *s, GlobalConfig *cfg)
{
  s->cfg = cfg;
}

static inline void
log_pipe_reset_config(LogPipe *s)
{
  log_pipe_set_config(s, NULL);
}

static inline gboolean
log_pipe_init(LogPipe *s)
{
  if (!(s->flags & PIF_INITIALIZED))
    {
      if (s->pre_init && !s->pre_init(s))
        return FALSE;
      if (!s->init || s->init(s))
        {
          s->flags |= PIF_INITIALIZED;
          return TRUE;
        }
      return FALSE;
    }
  return TRUE;
}

static inline gboolean
log_pipe_deinit(LogPipe *s)
{
  if ((s->flags & PIF_INITIALIZED))
    {
      if (!s->deinit || s->deinit(s))
        {
          s->flags &= ~PIF_INITIALIZED;

          if (s->post_deinit)
            s->post_deinit(s);
          return TRUE;
        }
      return FALSE;
    }
  return TRUE;
}

static inline gboolean
log_pipe_pre_config_init(LogPipe *s)
{
  if (s->pre_config_init)
    return s->pre_config_init(s);
  return TRUE;
}

static inline gboolean
log_pipe_post_config_init(LogPipe *s)
{
  if (s->post_config_init)
    return s->post_config_init(s);
  return TRUE;
}

static inline LogPipe *
log_pipe_clone(LogPipe *self)
{
  g_assert(NULL != self->clone);
  return self->clone(self);
}

static inline void
log_pipe_notify(LogPipe *s, gint notify_code, gpointer user_data)
{
  if (s->notify)
    s->notify(s, notify_code, user_data);
}

static inline void
log_pipe_append(LogPipe *s, LogPipe *next)
{
  s->pipe_next = next;
}

void log_pipe_set_persist_name(LogPipe *self, const gchar *persist_name);
const gchar *log_pipe_get_persist_name(const LogPipe *self);

void log_pipe_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options);
void log_pipe_forward_msg(LogPipe *self, LogMessage *msg, const LogPathOptions *path_options);

void log_pipe_set_options(LogPipe *self, const LogPipeOptions *options);
void log_pipe_set_internal(LogPipe *self, gboolean internal);
gboolean log_pipe_is_internal(const LogPipe *self);

void log_pipe_free_method(LogPipe *s);

static inline void
log_pipe_walk(LogPipe *s, LogPathWalkFunc func, gpointer user_data)
{
  return s->walk(s, func, user_data);
}

void log_pipe_add_info(LogPipe *self, const gchar *info);
#endif

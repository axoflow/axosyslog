/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#include "logmsg/logmsg.h"
#include "str-utils.h"
#include "str-repr/encode.h"
#include "messages.h"
#include "logpipe.h"
#include "timeutils/cache.h"
#include "timeutils/misc.h"
#include "logmsg/nvtable.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"
#include "template/templates.h"
#include "tls-support.h"
#include "compat/string.h"
#include "rcptid.h"
#include "template/macros.h"
#include "host-id.h"
#include "ack-tracker/ack_tracker.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "str-format.h"

#include <glib/gprintf.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>


/**********************************************************************
 * LogMessage
 **********************************************************************/

static inline gboolean
log_msg_chk_flag(const LogMessage *self, gint32 flag)
{
  return self->flags & flag;
}

static inline void
log_msg_set_flag(LogMessage *self, gint32 flag)
{
  self->flags |= flag;
}

static inline void
log_msg_set_host_id(LogMessage *msg)
{
  msg->host_id = host_id_get();
}

/* the index matches the value id */
const gchar *builtin_value_names[] =
{
  "HOST",
  "HOST_FROM",
  "MESSAGE",
  "PROGRAM",
  "PID",
  "MSGID",
  "SOURCE",
  "LEGACY_MSGHDR",
  "__RESERVED_LM_V_MAX",
  NULL,
};

const gchar *
log_msg_value_type_to_str(LogMessageValueType self)
{
  g_assert(self <= LM_VT_NONE);

  static const gchar *as_str[] =
  {
    [LM_VT_STRING] = "string",
    [LM_VT_JSON] = "json",
    [LM_VT_BOOLEAN] = "boolean",
    [__COMPAT_LM_VT_INT32] = "compat-int32",
    [LM_VT_INTEGER] = "int",
    [LM_VT_DOUBLE] = "double",
    [LM_VT_DATETIME] = "datetime",
    [LM_VT_LIST] = "list",
    [LM_VT_NULL] = "null",
    [LM_VT_BYTES] = "bytes",
    [LM_VT_PROTOBUF] = "protobuf",
    [LM_VT_NONE] = "none",
  };

  return as_str[self];
}

gboolean
log_msg_value_type_from_str(const gchar *in_str, LogMessageValueType *out_type)
{
  if (strcmp(in_str, "string") == 0)
    *out_type = LM_VT_STRING;
  else if (strcmp(in_str, "json") == 0 || strcmp(in_str, "literal") == 0)
    *out_type = LM_VT_JSON;
  else if (strcmp(in_str, "boolean") == 0)
    *out_type = LM_VT_BOOLEAN;
  else if (strcmp(in_str, "int32") == 0 || strcmp(in_str, "int") == 0 || strcmp(in_str, "int64") == 0
           || strcmp(in_str, "integer") == 0)
    *out_type = LM_VT_INTEGER;
  else if (strcmp(in_str, "double") == 0 || strcmp(in_str, "float") == 0)
    *out_type = LM_VT_DOUBLE;
  else if (strcmp(in_str, "datetime") == 0)
    *out_type = LM_VT_DATETIME;
  else if (strcmp(in_str, "list") == 0)
    *out_type = LM_VT_LIST;
  else if (strcmp(in_str, "null") == 0)
    *out_type = LM_VT_NULL;
  else if (strcmp(in_str, "bytes") == 0)
    *out_type = LM_VT_BYTES;
  else if (strcmp(in_str, "protobuf") == 0)
    *out_type = LM_VT_PROTOBUF;
  else if (strcmp(in_str, "none") == 0)
    *out_type = LM_VT_NONE;
  else
    return FALSE;

  return TRUE;
};

static void
__free_macro_value(void *val)
{
  g_string_free((GString *) val, TRUE);
}

static NVHandle match_handles[256];
NVRegistry *logmsg_registry;
const char logmsg_sd_prefix[] = ".SDATA.";
const gint logmsg_sd_prefix_len = sizeof(logmsg_sd_prefix) - 1;
gint logmsg_queue_node_max = 1;
/* statistics */
static StatsCounterItem *count_msg_clones;
static StatsCounterItem *count_payload_reallocs;
static StatsCounterItem *count_sdata_updates;
static StatsCounterItem *count_allocated_bytes;
static GPrivate priv_macro_value = G_PRIVATE_INIT(__free_macro_value);

static void
log_msg_update_sdata_slow(LogMessage *self, NVHandle handle, const gchar *name, gssize name_len)
{
  guint16 alloc_sdata;
  guint16 prefix_and_block_len;
  gint i;
  const gchar *dot;

  /* this was a structured data element, insert a ref to the sdata array */

  stats_counter_inc(count_sdata_updates);
  if (self->num_sdata == 255)
    {
      msg_error("syslog-ng only supports 255 SD elements right now, just drop an email to the mailing list that it was not enough with your use-case so we can increase it");
      return;
    }

  if (self->alloc_sdata <= self->num_sdata)
    {
      alloc_sdata = MAX(self->num_sdata + 1, STRICT_ROUND_TO_NEXT_EIGHT(self->num_sdata));
      if (alloc_sdata > 255)
        alloc_sdata = 255;
    }
  else
    alloc_sdata = self->alloc_sdata;

  if (log_msg_chk_flag(self, LF_STATE_OWN_SDATA) && self->sdata)
    {
      if (self->alloc_sdata < alloc_sdata)
        {
          self->sdata = g_realloc(self->sdata, alloc_sdata * sizeof(self->sdata[0]));
          memset(&self->sdata[self->alloc_sdata], 0, (alloc_sdata - self->alloc_sdata) * sizeof(self->sdata[0]));
        }
    }
  else
    {
      NVHandle *sdata;

      sdata = g_malloc(alloc_sdata * sizeof(self->sdata[0]));
      if (self->num_sdata)
        memcpy(sdata, self->sdata, self->num_sdata * sizeof(self->sdata[0]));
      memset(&sdata[self->num_sdata], 0, sizeof(self->sdata[0]) * (self->alloc_sdata - self->num_sdata));
      self->sdata = sdata;
      log_msg_set_flag(self, LF_STATE_OWN_SDATA);
    }
  guint16 old_alloc_sdata = self->alloc_sdata;
  self->alloc_sdata = alloc_sdata;
  if (self->sdata)
    {
      self->allocated_bytes += ((self->alloc_sdata - old_alloc_sdata) * sizeof(self->sdata[0]));
      stats_counter_add(count_allocated_bytes, (self->alloc_sdata - old_alloc_sdata) * sizeof(self->sdata[0]));
    }
  /* ok, we have our own SDATA array now which has at least one free slot */

  if (!self->initial_parse)
    {
      dot = memrchr(name, '.', name_len);
      prefix_and_block_len = dot - name;

      for (i = self->num_sdata - 1; i >= 0; i--)
        {
          gssize sdata_name_len;
          const gchar *sdata_name;

          sdata_name_len = 0;
          sdata_name = log_msg_get_value_name(self->sdata[i], &sdata_name_len);
          if (sdata_name_len > prefix_and_block_len &&
              strncmp(sdata_name, name, prefix_and_block_len) == 0)
            {
              /* ok we have found the last SDATA entry that has the same block */
              break;
            }
        }
      i++;
    }
  else
    i = -1;

  if (i >= 0)
    {
      memmove(&self->sdata[i+1], &self->sdata[i], (self->num_sdata - i) * sizeof(self->sdata[0]));
      self->sdata[i] = handle;
    }
  else
    {
      self->sdata[self->num_sdata] = handle;
    }
  self->num_sdata++;
}

static inline void
log_msg_update_sdata(LogMessage *self, NVHandle handle, const gchar *name, gssize name_len)
{
  if (log_msg_is_handle_sdata(handle))
    log_msg_update_sdata_slow(self, handle, name, name_len);
}

static inline void
log_msg_update_num_matches(LogMessage *self, NVHandle handle)
{
  if (log_msg_is_handle_match(handle))
    {
      gint index_ = log_msg_get_match_index(handle);

      /* the whole between num_matches and the new index is emptied out to
       * avoid leaking of stale values */

      for (gint i = self->num_matches; i < index_; i++)
        log_msg_unset_match(self, i);
      if (index_ >= self->num_matches)
        self->num_matches = index_ + 1;
    }
}

NVHandle
log_msg_get_value_handle(const gchar *value_name)
{
  NVHandle handle;

  handle = nv_registry_alloc_handle(logmsg_registry, value_name);

  /* check if name starts with sd_prefix and has at least one additional character */
  if (strncmp(value_name, logmsg_sd_prefix, logmsg_sd_prefix_len) == 0 && value_name[6])
    {
      nv_registry_set_handle_flags(logmsg_registry, handle, LM_VF_SDATA);
    }

  return handle;
}

gboolean
log_msg_is_value_name_valid(const gchar *value)
{
  if (strncmp(value, logmsg_sd_prefix, logmsg_sd_prefix_len) == 0)
    {
      const gchar *dot;
      int dot_found = 0;

      dot = strchr(value, '.');
      while (dot && strlen(dot) > 1)
        {
          dot_found++;
          dot++;
          dot = strchr(dot, '.');
        }
      return (dot_found >= 3);
    }
  else
    return TRUE;
}

const gchar *
log_msg_get_macro_value(const LogMessage *self, gint id, gssize *value_len, LogMessageValueType *type)
{
  GString *value;

  value = g_private_get(&priv_macro_value);
  if (!value)
    {
      value = g_string_sized_new(256);
      g_private_replace(&priv_macro_value, value);
    }
  g_string_truncate(value, 0);

  log_macro_expand_simple(id, self, value, type);
  if (value_len)
    *value_len = value->len;
  return value->str;
}

static void
log_msg_init_queue_node(LogMessage *msg, LogMessageQueueNode *node, const LogPathOptions *path_options)
{
  INIT_IV_LIST_HEAD(&node->list);
  node->ack_needed = path_options->ack_needed;
  node->flow_control_requested = path_options->flow_control_requested;
  node->msg = log_msg_ref(msg);
}

/*
 * Allocates a new LogMessageQueueNode instance to be enqueued in a
 * LogQueue.
 *
 * NOTE: Assumed to be running in the source thread, and that the same
 * LogMessage instance is only put into queue from the same thread (e.g.
 * the related fields are _NOT_ locked).
 */
LogMessageQueueNode *
log_msg_alloc_queue_node(LogMessage *msg, const LogPathOptions *path_options)
{
  LogMessageQueueNode *node;

  if (msg->cur_node < msg->num_nodes)
    {
      node = &msg->nodes[msg->cur_node++];
      node->embedded = TRUE;
    }
  else
    {
      gint nodes = (volatile gint) logmsg_queue_node_max;

      /* this is a racy update, but it doesn't really hurt if we lose an
       * update or if we continue with a smaller value in parallel threads
       * for some time yet, since the smaller number only means that we
       * pre-allocate somewhat less LogMsgQueueNodes in the message
       * structure, but will be fine regardless (if we'd overflow the
       * pre-allocated space, we start allocating nodes dynamically from
       * heap.
       */
      if (nodes < 32 && nodes <= msg->num_nodes)
        logmsg_queue_node_max = msg->num_nodes + 1;
      node = g_slice_new(LogMessageQueueNode);
      node->embedded = FALSE;
    }
  log_msg_init_queue_node(msg, node, path_options);
  return node;
}

LogMessageQueueNode *
log_msg_alloc_dynamic_queue_node(LogMessage *msg, const LogPathOptions *path_options)
{
  LogMessageQueueNode *node;
  node = g_slice_new(LogMessageQueueNode);
  node->embedded = FALSE;
  log_msg_init_queue_node(msg, node, path_options);
  return node;
}

void
log_msg_free_queue_node(LogMessageQueueNode *node)
{
  if (!node->embedded)
    g_slice_free(LogMessageQueueNode, node);
}

static gboolean
_log_name_value_updates(LogMessage *self)
{
  /* we don't log name value updates for internal messages that are
   * initialized at this point, as that may generate an endless recursion.
   * log_msg_new_internal() calling log_msg_set_value(), which in turn
   * generates an internal message, again calling log_msg_set_value()
   */
  return (self->flags & LF_INTERNAL) == 0;
}

static inline gboolean
_value_invalidates_legacy_header(NVHandle handle)
{
  return handle == LM_V_PROGRAM || handle == LM_V_PID;
}

void
log_msg_rename_value(LogMessage *self, NVHandle from, NVHandle to)
{
  if (from == to)
    return;

  gssize value_len = 0;
  LogMessageValueType type;
  const gchar *value = log_msg_get_value_if_set_with_type(self, from, &value_len, &type);
  if (!value)
    return;

  log_msg_set_value_with_type(self, to, value, value_len, type);
  log_msg_unset_value(self, from);
}

void
log_msg_set_value_with_type(LogMessage *self, NVHandle handle,
                            const gchar *value, gssize value_len,
                            LogMessageValueType type)
{
  const gchar *name;
  gssize name_len;
  gboolean new_entry = FALSE;

  g_assert(!log_msg_is_write_protected(self));

  if (handle == LM_V_NONE)
    return;

  name_len = 0;
  name = log_msg_get_value_name(handle, &name_len);

  if (value_len < 0)
    value_len = strlen(value);

  self->generation++;
  if (_log_name_value_updates(self))
    {
      msg_trace("Setting value",
                evt_tag_str("name", name),
                evt_tag_mem("value", value, value_len),
                evt_tag_str("type", log_msg_value_type_to_str(type)),
                evt_tag_msg_reference(self));
    }

  if (!log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD))
    {
      self->payload = nv_table_clone(self->payload, name_len + value_len + 2);
      log_msg_set_flag(self, LF_STATE_OWN_PAYLOAD);
      self->allocated_bytes += self->payload->size;
      stats_counter_add(count_allocated_bytes, self->payload->size);
    }

  /* we need a loop here as a single realloc may not be enough. Might help
   * if we pass how much bytes we need though. */

  while (!nv_table_add_value(self->payload, handle, name, name_len, value, value_len, type, &new_entry))
    {
      /* error allocating string in payload, reallocate */
      guint32 old_size = self->payload->size;
      if (!nv_table_realloc(self->payload, &self->payload))
        {
          /* can't grow the payload, it has reached the maximum size */
          msg_info("Cannot store value for this log message, maximum size has been reached",
                   evt_tag_int("maximum_payload", NV_TABLE_MAX_BYTES),
                   evt_tag_str("name", name),
                   evt_tag_printf("value", "%.32s%s", value, value_len > 32 ? "..." : ""));
          break;
        }
      guint32 new_size = self->payload->size;
      self->allocated_bytes += (new_size - old_size);
      stats_counter_add(count_allocated_bytes, new_size-old_size);
      stats_counter_inc(count_payload_reallocs);
    }

  if (new_entry)
    log_msg_update_sdata(self, handle, name, name_len);
  log_msg_update_num_matches(self, handle);

  if (_value_invalidates_legacy_header(handle))
    log_msg_unset_value(self, LM_V_LEGACY_MSGHDR);
}

void
log_msg_set_value(LogMessage *self, NVHandle handle, const gchar *value, gssize value_len)
{
  log_msg_set_value_with_type(self, handle, value, value_len, LM_VT_STRING);
}

void
log_msg_unset_value(LogMessage *self, NVHandle handle)
{
  g_assert(!log_msg_is_write_protected(self));

  self->generation++;
  if (_log_name_value_updates(self))
    {
      msg_trace("Unsetting value",
                evt_tag_str("name", log_msg_get_value_name(handle, NULL)),
                evt_tag_msg_reference(self));
    }

  if (!log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD))
    {
      self->payload = nv_table_clone(self->payload, 0);
      log_msg_set_flag(self, LF_STATE_OWN_PAYLOAD);
    }

  while (!nv_table_unset_value(self->payload, handle))
    {
      /* error allocating string in payload, reallocate */
      guint32 old_size = self->payload->size;
      if (!nv_table_realloc(self->payload, &self->payload))
        {
          /* can't grow the payload, it has reached the maximum size */
          const gchar *name = log_msg_get_value_name(handle, NULL);
          msg_info("Cannot unset value for this log message, maximum size has been reached",
                   evt_tag_int("maximum_payload", NV_TABLE_MAX_BYTES),
                   evt_tag_str("name", name));
          break;
        }
      guint32 new_size = self->payload->size;
      self->allocated_bytes += (new_size - old_size);
      stats_counter_add(count_allocated_bytes, new_size-old_size);
      stats_counter_inc(count_payload_reallocs);
    }

  if (_value_invalidates_legacy_header(handle))
    log_msg_unset_value(self, LM_V_LEGACY_MSGHDR);
}

void
log_msg_unset_value_by_name(LogMessage *self, const gchar *name)
{
  log_msg_unset_value(self, log_msg_get_value_handle(name));
}

void
log_msg_set_value_indirect_with_type(LogMessage *self, NVHandle handle,
                                     NVHandle ref_handle, guint16 ofs, guint16 len,
                                     LogMessageValueType type)
{
  const gchar *name;
  gssize name_len;
  gboolean new_entry = FALSE;

  g_assert(!log_msg_is_write_protected(self));

  if (handle == LM_V_NONE)
    return;

  g_assert(handle >= LM_V_MAX);

  name_len = 0;
  name = log_msg_get_value_name(handle, &name_len);

  self->generation++;
  if (_log_name_value_updates(self))
    {
      msg_trace("Setting indirect value",
                evt_tag_str("name", name),
                evt_tag_str("type", log_msg_value_type_to_str(type)),
                evt_tag_int("ref_handle", ref_handle),
                evt_tag_int("ofs", ofs),
                evt_tag_int("len", len),
                evt_tag_msg_reference(self));
    }

  if (!log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD))
    {
      self->payload = nv_table_clone(self->payload, name_len + 1);
      log_msg_set_flag(self, LF_STATE_OWN_PAYLOAD);
    }

  NVReferencedSlice referenced_slice =
  {
    .handle = ref_handle,
    .ofs = ofs,
    .len = len,
  };

  while (!nv_table_add_value_indirect(self->payload, handle, name, name_len, &referenced_slice, type, &new_entry))
    {
      /* error allocating string in payload, reallocate */
      if (!nv_table_realloc(self->payload, &self->payload))
        {
          /* error growing the payload, skip without storing the value */
          msg_info("Cannot store referenced value for this log message, maximum size has been reached",
                   evt_tag_str("name", name),
                   evt_tag_str("ref-name", log_msg_get_value_name(ref_handle, NULL)));
          break;
        }
      stats_counter_inc(count_payload_reallocs);
    }

  if (new_entry)
    log_msg_update_sdata(self, handle, name, name_len);
  log_msg_update_num_matches(self, handle);
}

void
log_msg_set_value_indirect(LogMessage *self, NVHandle handle, NVHandle ref_handle,
                           guint16 ofs, guint16 len)
{
  log_msg_set_value_indirect_with_type(self, handle, ref_handle, ofs, len, LM_VT_STRING);
}

gboolean
log_msg_values_foreach(const LogMessage *self, NVTableForeachFunc func, gpointer user_data)
{
  return nv_table_foreach(self->payload, logmsg_registry, func, user_data);
}

NVHandle
log_msg_get_match_handle(gint index_)
{
  if (index_ >= 0 && index_ < LOGMSG_MAX_MATCHES)
    return match_handles[index_];

  return LM_V_NONE;
}

gint
log_msg_get_match_index(NVHandle handle)
{
  gint index_ = handle - match_handles[0];

  g_assert(index_ >= 0 && index_ < LOGMSG_MAX_MATCHES);
  return index_;
}

void
log_msg_set_match_with_type(LogMessage *self, gint index_,
                            const gchar *value, gssize value_len,
                            LogMessageValueType type)
{
  if (index_ >= 0 && index_ < LOGMSG_MAX_MATCHES)
    log_msg_set_value_with_type(self, match_handles[index_], value, value_len, type);
}

void
log_msg_set_match(LogMessage *self, gint index_, const gchar *value, gssize value_len)
{
  log_msg_set_match_with_type(self, index_, value, value_len, LM_VT_STRING);
}


void
log_msg_set_match_indirect_with_type(LogMessage *self, gint index_,
                                     NVHandle ref_handle, guint16 ofs, guint16 len,
                                     LogMessageValueType type)
{
  if (index_ >= 0 && index_ < LOGMSG_MAX_MATCHES)
    log_msg_set_value_indirect_with_type(self, match_handles[index_], ref_handle, ofs, len, type);
}

void
log_msg_set_match_indirect(LogMessage *self, gint index_, NVHandle ref_handle, guint16 ofs, guint16 len)
{
  log_msg_set_match_indirect_with_type(self, index_, ref_handle, ofs, len, LM_VT_STRING);
}

const gchar *
log_msg_get_match_if_set_with_type(const LogMessage *self, gint index_, gssize *value_len,
                                   LogMessageValueType *type)
{
  if (index_ >= 0 && index_ < LOGMSG_MAX_MATCHES)
    return nv_table_get_value(self->payload, match_handles[index_], value_len, type);
  return NULL;
}

const gchar *
log_msg_get_match_with_type(const LogMessage *self, gint index_, gssize *value_len,
                            LogMessageValueType *type)
{
  const gchar *result = log_msg_get_match_if_set_with_type(self, index_, value_len, type);

  if (result)
    return result;

  if (value_len)
    *value_len = 0;
  if (type)
    *type = LM_VT_NULL;
  return "";
}

const gchar *
log_msg_get_match(const LogMessage *self, gint index_, gssize *value_len)
{
  return log_msg_get_match_with_type(self, index_, value_len, NULL);
}

void
log_msg_unset_match(LogMessage *self, gint index_)
{
  if (index_ >= 0 && index_ < LOGMSG_MAX_MATCHES)
    log_msg_unset_value(self, match_handles[index_]);
}

void
log_msg_truncate_matches(LogMessage *self, gint n)
{
  if (n < 0)
    n = 0;
  for (gint i = n; i < self->num_matches; i++)
    log_msg_unset_match(self, i);
  self->num_matches = n;
}

void
log_msg_clear_matches(LogMessage *self)
{
  log_msg_truncate_matches(self, 0);
}

#if GLIB_SIZEOF_LONG != GLIB_SIZEOF_VOID_P
#error "The tags bit array assumes that long is the same size as the pointer"
#endif

#if GLIB_SIZEOF_LONG == 8
#define LOGMSG_TAGS_NDX_SHIFT 6
#define LOGMSG_TAGS_NDX_MASK  0x3F
#define LOGMSG_TAGS_BITS      64
#elif GLIB_SIZEOF_LONG == 4
#define LOGMSG_TAGS_NDX_SHIFT 5
#define LOGMSG_TAGS_NDX_MASK  0x1F
#define LOGMSG_TAGS_BITS      32
#else
#error "Unsupported word length, only 32 or 64 bit platforms are supported"
#endif

static inline void
log_msg_tags_foreach_item(const LogMessage *self, gint base, gulong item, LogMessageTagsForeachFunc callback,
                          gpointer user_data)
{
  gint i;

  for (i = 0; i < LOGMSG_TAGS_BITS; i++)
    {
      if (G_LIKELY(!item))
        return;
      if (item & 1)
        {
          LogTagId id = (LogTagId) base + i;

          callback(self, id, log_tags_get_by_id(id), user_data);
        }
      item >>= 1;
    }
}


void
log_msg_tags_foreach(const LogMessage *self, LogMessageTagsForeachFunc callback, gpointer user_data)
{
  guint i;

  if (self->num_tags == 0)
    {
      log_msg_tags_foreach_item(self, 0, (gulong) self->tags, callback, user_data);
    }
  else
    {
      for (i = 0; i != self->num_tags; ++i)
        {
          log_msg_tags_foreach_item(self, i * LOGMSG_TAGS_BITS, self->tags[i], callback, user_data);
        }
    }
}


static inline void
log_msg_set_bit(gulong *tags, gint index_, gboolean value)
{
  if (value)
    tags[index_ >> LOGMSG_TAGS_NDX_SHIFT] |= ((gulong) (1UL << (index_ & LOGMSG_TAGS_NDX_MASK)));
  else
    tags[index_ >> LOGMSG_TAGS_NDX_SHIFT] &= ~((gulong) (1UL << (index_ & LOGMSG_TAGS_NDX_MASK)));
}

static inline gboolean
log_msg_get_bit(gulong *tags, gint index_)
{
  return !!(tags[index_ >> LOGMSG_TAGS_NDX_SHIFT] & ((gulong) (1UL << (index_ & LOGMSG_TAGS_NDX_MASK))));
}

void
log_msg_set_tag_by_id_onoff(LogMessage *self, LogTagId id, gboolean on)
{
  gulong *old_tags;
  gint old_num_tags;
  gboolean inline_tags;

  g_assert(!log_msg_is_write_protected(self));

  self->generation++;
  msg_trace("Setting tag",
            evt_tag_str("name", log_tags_get_by_id(id)),
            evt_tag_int("value", on),
            evt_tag_printf("msg", "%p", self));
  if (!log_msg_chk_flag(self, LF_STATE_OWN_TAGS) && self->num_tags)
    {
      self->tags = g_memdup2(self->tags, sizeof(self->tags[0]) * self->num_tags);
    }
  log_msg_set_flag(self, LF_STATE_OWN_TAGS);

  /* if num_tags is 0, it means that we use inline storage of tags */
  inline_tags = self->num_tags == 0;
  if (inline_tags && id < LOGMSG_TAGS_BITS)
    {
      /* store this tag inline */
      log_msg_set_bit((gulong *) &self->tags, id, on);
    }
  else
    {
      /* we can't put this tag inline, either because it is too large, or we don't have the inline space any more */

      if ((self->num_tags * LOGMSG_TAGS_BITS) <= id)
        {
          if (G_UNLIKELY(8159 < id))
            {
              msg_error("Maximum number of tags reached");
              return;
            }
          old_num_tags = self->num_tags;
          self->num_tags = (id / LOGMSG_TAGS_BITS) + 1;

          old_tags = self->tags;
          if (old_num_tags)
            self->tags = g_realloc(self->tags, sizeof(self->tags[0]) * self->num_tags);
          else
            self->tags = g_malloc(sizeof(self->tags[0]) * self->num_tags);
          memset(&self->tags[old_num_tags], 0, (self->num_tags - old_num_tags) * sizeof(self->tags[0]));

          if (inline_tags)
            self->tags[0] = (gulong) old_tags;
        }

      log_msg_set_bit(self->tags, id, on);
    }
  if (on)
    {
      log_tags_inc_counter(id);
    }
  else
    {
      log_tags_dec_counter(id);
    }
}

void
log_msg_set_tag_by_id(LogMessage *self, LogTagId id)
{
  log_msg_set_tag_by_id_onoff(self, id, TRUE);
}

void
log_msg_set_tag_by_name(LogMessage *self, const gchar *name)
{
  log_msg_set_tag_by_id_onoff(self, log_tags_get_by_name(name), TRUE);
}

void
log_msg_clear_tag_by_id(LogMessage *self, LogTagId id)
{
  log_msg_set_tag_by_id_onoff(self, id, FALSE);
}

void
log_msg_clear_tag_by_name(LogMessage *self, const gchar *name)
{
  log_msg_set_tag_by_id_onoff(self, log_tags_get_by_name(name), FALSE);
}

gboolean
log_msg_is_tag_by_id(LogMessage *self, LogTagId id)
{
  if (G_UNLIKELY(8159 < id))
    {
      msg_error("Invalid tag", evt_tag_int("id", (gint) id));
      return FALSE;
    }
  if (self->num_tags == 0 && id < LOGMSG_TAGS_BITS)
    return log_msg_get_bit((gulong *) &self->tags, id);
  else if (id < self->num_tags * LOGMSG_TAGS_BITS)
    return log_msg_get_bit(self->tags, id);
  else
    return FALSE;
}

gboolean
log_msg_is_tag_by_name(LogMessage *self, const gchar *name)
{
  return log_msg_is_tag_by_id(self, log_tags_get_by_name(name));
}

/* structured data elements */

static void
log_msg_sdata_append_key_escaped(GString *result, const gchar *sstr, gssize len)
{
  /* The specification does not have any way to escape keys.
   * The goal is to create syntactically valid structured data fields. */
  const guchar *ustr = (const guchar *) sstr;

  for (gssize i = 0; i < len; i++)
    {
      if (!isascii(ustr[i]) || ustr[i] == '=' || ustr[i] == ' '
          || ustr[i] == '[' || ustr[i] == ']' || ustr[i] == '"')
        {
          gchar hex_code[4];
          g_sprintf(hex_code, "%%%02X", ustr[i]);
          g_string_append(result, hex_code);
        }
      else
        g_string_append_c(result, ustr[i]);
    }
}

static void
log_msg_sdata_append_escaped(GString *result, const gchar *sstr, gssize len)
{
  gint i;
  const guchar *ustr = (const guchar *) sstr;

  for (i = 0; i < len; i++)
    {
      if (ustr[i] == '"' || ustr[i] == '\\' || ustr[i] == ']')
        {
          g_string_append_c(result, '\\');
          g_string_append_c(result, ustr[i]);
        }
      else
        g_string_append_c(result, ustr[i]);
    }
}

void
log_msg_append_format_sdata(const LogMessage *self, GString *result,  guint32 seq_num)
{
  const gchar *value;
  const gchar *sdata_name, *sdata_elem, *sdata_param, *cur_elem = NULL, *dot;
  gssize sdata_name_len, sdata_elem_len, sdata_param_len, cur_elem_len = 0, len;
  gint i;
  static NVHandle meta_seqid = 0;
  gssize seqid_length;
  gboolean has_seq_num = FALSE;
  const gchar *seqid;

  if (!meta_seqid)
    meta_seqid = log_msg_get_value_handle(".SDATA.meta.sequenceId");

  seqid = log_msg_get_value(self, meta_seqid, &seqid_length);
  APPEND_ZERO(seqid, seqid, seqid_length);
  if (seqid[0])
    /* Message stores sequenceId */
    has_seq_num = TRUE;
  else
    /* Message hasn't sequenceId */
    has_seq_num = FALSE;

  for (i = 0; i < self->num_sdata; i++)
    {
      NVHandle handle = self->sdata[i];
      guint16 handle_flags;
      gint sd_id_len;

      sdata_name_len = 0;
      sdata_name = log_msg_get_value_name(handle, &sdata_name_len);
      handle_flags = nv_registry_get_handle_flags(logmsg_registry, handle);
      value = log_msg_get_value_if_set(self, handle, &len);
      if (!value)
        continue;

      g_assert(handle_flags & LM_VF_SDATA);

      /* sdata_name always begins with .SDATA. */
      g_assert(sdata_name_len > 6);

      sdata_elem = sdata_name + 7;
      sd_id_len = (handle_flags >> 8);

      if (sd_id_len)
        {
          dot = sdata_elem + sd_id_len;
          if (dot - sdata_name != sdata_name_len)
            {
              g_assert((dot - sdata_name < sdata_name_len) && *dot == '.');
            }
          else
            {
              /* Standalone sdata e.g. [[UserData.updatelist@18372.4]] */
              dot = NULL;
            }
        }
      else
        {
          dot = memrchr(sdata_elem, '.', sdata_name_len - 7);
        }

      if (G_LIKELY(dot))
        {
          sdata_elem_len = dot - sdata_elem;

          sdata_param = dot + 1;
          sdata_param_len = sdata_name_len - (dot + 1 - sdata_name);
        }
      else
        {
          sdata_elem_len = sdata_name_len - 7;
          if (sdata_elem_len == 0)
            {
              sdata_elem = "none";
              sdata_elem_len = 4;
            }
          sdata_param = "";
          sdata_param_len = 0;
        }
      if (!cur_elem || sdata_elem_len != cur_elem_len || strncmp(cur_elem, sdata_elem, sdata_elem_len) != 0)
        {
          if (cur_elem)
            {
              /* close the previous block */
              g_string_append_c(result, ']');
            }

          /* the current SD block has changed, emit a start */
          g_string_append_c(result, '[');
          log_msg_sdata_append_key_escaped(result, sdata_elem, sdata_elem_len);

          /* update cur_elem */
          cur_elem = sdata_elem;
          cur_elem_len = sdata_elem_len;
        }
      /* if message hasn't sequenceId and the cur_elem is the meta block Append the sequenceId for the result
         if seq_num isn't 0 */
      if (!has_seq_num && seq_num!=0 && strncmp(sdata_elem, "meta.", 5) == 0)
        {
          GString *sequence_id = scratch_buffers_alloc();
          format_uint64_padded(sequence_id, 0, 0, 10, seq_num);
          g_string_append_c(result, ' ');
          g_string_append_len(result, "sequenceId=\"", 12);
          g_string_append_len(result, sequence_id->str, sequence_id->len);
          g_string_append_c(result, '"');
          has_seq_num = TRUE;
        }
      if (sdata_param_len)
        {
          if (value)
            {
              g_string_append_c(result, ' ');
              log_msg_sdata_append_key_escaped(result, sdata_param, sdata_param_len);
              g_string_append(result, "=\"");
              log_msg_sdata_append_escaped(result, value, len);
              g_string_append_c(result, '"');
            }
        }
    }
  if (cur_elem)
    {
      g_string_append_c(result, ']');
    }
  /*
    There was no meta block and if sequenceId must be added (seq_num!=0)
    create the whole meta block with sequenceId
  */
  if (!has_seq_num && seq_num!=0)
    {
      GString *sequence_id = scratch_buffers_alloc();
      format_uint64_padded(sequence_id, 0, 0, 10, seq_num);

      g_string_append_c(result, '[');
      g_string_append_len(result, "meta sequenceId=\"", 17);
      g_string_append_len(result, sequence_id->str, sequence_id->len);
      g_string_append_len(result, "\"]", 2);
    }
}

void
log_msg_format_sdata(const LogMessage *self, GString *result,  guint32 seq_num)
{
  g_string_truncate(result, 0);
  log_msg_append_format_sdata(self, result, seq_num);
}

void
log_msg_clear_sdata(LogMessage *self)
{
  for (gint i = 0; i < self->num_sdata; i++)
    log_msg_unset_value(self, self->sdata[i]);
  if (!log_msg_chk_flag(self, LF_STATE_OWN_SDATA))
    {
      self->sdata = NULL;
      self->alloc_sdata = 0;
    }
  self->num_sdata = 0;
}

gboolean
log_msg_append_tags_callback(const LogMessage *self, LogTagId tag_id, const gchar *name, gpointer user_data)
{
  GString *result = (GString *) ((gpointer *) user_data)[0];
  gint original_length = GPOINTER_TO_UINT(((gpointer *) user_data)[1]);
  gboolean include_localtags = GPOINTER_TO_UINT(((gpointer *) user_data)[2]);

  g_assert(result);

  if (result->len > original_length)
    g_string_append_c(result, ',');

  if (include_localtags || name[0] != '.')
    str_repr_encode_append(result, name, -1, ",");
  return TRUE;
}

void
log_msg_format_tags(const LogMessage *self, GString *result, gboolean include_localtags)
{
  gpointer args[] = { result, GUINT_TO_POINTER(result->len), GUINT_TO_POINTER(include_localtags) };

  log_msg_tags_foreach(self, log_msg_append_tags_callback, args);
}

void
log_msg_format_matches(const LogMessage *self, GString *result)
{
  gsize original_length = result->len;

  for (gint i = 1; i < self->num_matches; i++)
    {
      if (result->len > original_length)
        g_string_append_c(result, ',');

      gssize len;
      const gchar *m = log_msg_get_match(self, i, &len);
      str_repr_encode_append(result, m, len, ",");
    }
}

void
log_msg_set_saddr(LogMessage *self, GSockAddr *saddr)
{
  log_msg_set_saddr_ref(self, g_sockaddr_ref(saddr));
}

void
log_msg_set_saddr_ref(LogMessage *self, GSockAddr *saddr)
{
  if (log_msg_chk_flag(self, LF_STATE_OWN_SADDR))
    g_sockaddr_unref(self->saddr);
  self->saddr = saddr;
  self->flags |= LF_STATE_OWN_SADDR;
}

void
log_msg_set_daddr(LogMessage *self, GSockAddr *daddr)
{
  log_msg_set_daddr_ref(self, g_sockaddr_ref(daddr));
}

void
log_msg_set_daddr_ref(LogMessage *self, GSockAddr *daddr)
{
  if (log_msg_chk_flag(self,  LF_STATE_OWN_DADDR))
    g_sockaddr_unref(self->daddr);
  self->daddr = daddr;
  self->flags |= LF_STATE_OWN_DADDR;
}

/**
 * log_msg_init:
 * @self: LogMessage instance
 * @saddr: sender address
 *
 * This function initializes a LogMessage instance without allocating it
 * first. It is used internally by the log_msg_new function.
 **/
static void
log_msg_init(LogMessage *self)
{
  /* ref is set to 1, ack is set to 0 */
  self->ack_and_ref_and_abort_and_suspended = LOGMSG_REFCACHE_REF_TO_VALUE(1);
  unix_time_set_now(&self->timestamps[LM_TS_RECVD]);
  self->timestamps[LM_TS_STAMP] = self->timestamps[LM_TS_RECVD];
  unix_time_unset(&self->timestamps[LM_TS_PROCESSED]);

  self->sdata = NULL;
  self->saddr = NULL;
  self->daddr = NULL;

  self->original = NULL;
  self->flags |= LF_STATE_OWN_MASK;
  self->pri = LOG_USER | LOG_NOTICE;

  self->rcptid = rcptid_generate_id();
  log_msg_set_host_id(self);
}

void
log_msg_clear(LogMessage *self)
{
  g_assert(!log_msg_is_write_protected(self));

  self->generation++;
  if(log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD))
    nv_table_unref(self->payload);
  self->payload = nv_table_new(LM_V_MAX, 16, 256);

  if (log_msg_chk_flag(self, LF_STATE_OWN_TAGS) && self->tags)
    {
      gboolean inline_tags = self->num_tags == 0;

      if (inline_tags)
        self->tags = NULL;
      else
        memset(self->tags, 0, self->num_tags * sizeof(self->tags[0]));
    }
  else
    {
      self->tags = NULL;
      self->num_tags = 0;
    }

  log_msg_clear_matches(self);
  log_msg_clear_sdata(self);

  if (log_msg_chk_flag(self, LF_STATE_OWN_SADDR))
    g_sockaddr_unref(self->saddr);
  self->saddr = NULL;
  if (log_msg_chk_flag(self, LF_STATE_OWN_DADDR))
    g_sockaddr_unref(self->daddr);
  self->daddr = NULL;

  /* clear "local", "utf8", "internal", "mark" and similar flags, we start afresh */
  self->flags = LF_STATE_OWN_MASK;
}

static inline LogMessage *
log_msg_alloc(gsize payload_size)
{
  LogMessage *msg;
  gsize payload_space = payload_size ? nv_table_get_alloc_size(LM_V_MAX, 16, payload_size) : 0;
  gsize alloc_size, payload_ofs = 0;

  /* NOTE: logmsg_node_max is updated from parallel threads without locking. */
  gint nodes = (volatile gint) logmsg_queue_node_max;

  alloc_size = sizeof(LogMessage) + sizeof(LogMessageQueueNode) * nodes;
  /* align to 8 boundary */
  if (payload_size)
    {
      alloc_size = (alloc_size + 7) & ~7;
      payload_ofs = alloc_size;
      alloc_size += payload_space;
    }
  msg = g_malloc(alloc_size);

  memset(msg, 0, sizeof(LogMessage));

  if (payload_size)
    msg->payload = nv_table_init_borrowed(((gchar *) msg) + payload_ofs, payload_space, LM_V_MAX);

  msg->num_nodes = nodes;
  msg->allocated_bytes = alloc_size + payload_space;
  stats_counter_add(count_allocated_bytes, msg->allocated_bytes);
  return msg;
}

static gboolean
_merge_value(NVHandle handle,
             const gchar *name, const gchar *value, gssize value_len,
             LogMessageValueType type, gpointer user_data)
{
  LogMessage *msg = (LogMessage *) user_data;

  if (!log_msg_is_value_set(msg, handle))
    log_msg_set_value_with_type(msg, handle, value, value_len, type);
  return FALSE;
}

void
log_msg_merge_context(LogMessage *self, LogMessage **context, gsize context_len)
{
  gint i;

  for (i = context_len - 1; i >= 0; i--)
    {
      LogMessage *msg_to_be_merged = context[i];

      log_msg_values_foreach(msg_to_be_merged, _merge_value, self);
    }
}

static void
log_msg_clone_ack(LogMessage *msg, AckType ack_type)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;

  g_assert(msg->original);
  path_options.ack_needed = TRUE;
  log_msg_ack(msg->original, &path_options, ack_type);
}

static inline LogMessage *
log_msg_alloc_clone(LogMessage *original)
{
  LogMessage *msg;

  /* NOTE: logmsg_node_max is updated from parallel threads without locking. */
  gint nodes = (volatile gint) logmsg_queue_node_max;

  gsize alloc_size = sizeof(LogMessage) + sizeof(LogMessageQueueNode) * nodes;
  msg = g_malloc(alloc_size);

  memcpy(msg, original, sizeof(*msg));
  msg->num_nodes = nodes;
  msg->allocated_bytes = alloc_size;
  stats_counter_add(count_allocated_bytes, msg->allocated_bytes);
  return msg;
}

/*
 * log_msg_clone_cow:
 *
 * Clone a copy-on-write (cow) copy of a log message.
 */
LogMessage *
log_msg_clone_cow(LogMessage *msg, const LogPathOptions *path_options)
{
  LogMessage *self = log_msg_alloc_clone(msg);

  stats_counter_inc(count_msg_clones);
  log_msg_write_protect(msg);

  msg_trace("Message was cloned",
            evt_tag_printf("original_msg", "%p", msg),
            evt_tag_msg_reference(self));

  /* every field _must_ be initialized explicitly if its direct
   * copying would cause problems (like copying a pointer by value) */

  /* reference the original message */
  self->original = log_msg_ref(msg);
  self->ack_and_ref_and_abort_and_suspended = LOGMSG_REFCACHE_REF_TO_VALUE(1) + LOGMSG_REFCACHE_ACK_TO_VALUE(
                                                0) + LOGMSG_REFCACHE_ABORT_TO_VALUE(0);
  self->cur_node = 0;
  self->write_protected = FALSE;

  log_msg_add_ack(self, path_options);
  if (!path_options->ack_needed)
    {
      self->ack_func = NULL;
    }
  else
    {
      self->ack_func = log_msg_clone_ack;
    }

  self->flags &= ~(LF_STATE_MASK - LF_STATE_CLONED_MASK);

  if (self->num_tags == 0)
    self->flags |= LF_STATE_OWN_TAGS;
  return self;
}

LogMessage *
log_msg_sized_new(gsize payload_size)
{
  LogMessage *self = log_msg_alloc(payload_size);

  log_msg_init(self);
  return self;
}

LogMessage *
log_msg_new_empty(void)
{
  return log_msg_sized_new(256);
}

/* This function creates a new log message that should be considered local */
LogMessage *
log_msg_new_local(void)
{
  LogMessage *self = log_msg_new_empty();

  self->flags |= LF_LOCAL;
  return self;
}


/**
 * log_msg_new_internal:
 * @prio: message priority (LOG_*)
 * @msg: message text
 * @flags: parse flags (LP_*)
 *
 * This function creates a new log message for messages originating
 * internally to syslog-ng
 **/
LogMessage *
log_msg_new_internal(gint prio, const gchar *msg)
{
  gchar buf[32];
  LogMessage *self;

  g_snprintf(buf, sizeof(buf), "%d", (int) getpid());
  self = log_msg_new_local();
  self->flags |= LF_INTERNAL;
  self->initial_parse = TRUE;
  log_msg_set_value(self, LM_V_PROGRAM, "syslog-ng", 9);
  log_msg_set_value(self, LM_V_PID, buf, -1);
  log_msg_set_value(self, LM_V_MESSAGE, msg, -1);
  self->initial_parse = FALSE;
  self->pri = prio;

  return self;
}

/**
 * log_msg_new_mark:
 *
 * This function returns a new MARK message. MARK messages have the LF_MARK
 * flag set.
 **/
LogMessage *
log_msg_new_mark(void)
{
  LogMessage *self = log_msg_new_local();

  log_msg_set_value(self, LM_V_MESSAGE, "-- MARK --", 10);
  self->pri = LOG_SYSLOG | LOG_INFO;
  self->flags |= LF_MARK | LF_INTERNAL;
  return self;
}

/**
 * log_msg_free:
 * @self: LogMessage instance
 *
 * Frees a LogMessage instance.
 **/
void
_log_msg_free(LogMessage *self)
{
  if (log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD) && self->payload)
    nv_table_unref(self->payload);
  if (log_msg_chk_flag(self, LF_STATE_OWN_TAGS) && self->tags && self->num_tags > 0)
    g_free(self->tags);

  if (log_msg_chk_flag(self, LF_STATE_OWN_SDATA) && self->sdata)
    g_free(self->sdata);
  if (log_msg_chk_flag(self, LF_STATE_OWN_SADDR))
    g_sockaddr_unref(self->saddr);
  if (log_msg_chk_flag(self, LF_STATE_OWN_DADDR))
    g_sockaddr_unref(self->daddr);

  if (self->original)
    log_msg_unref(self->original);

  stats_counter_sub(count_allocated_bytes, self->allocated_bytes);

  g_free(self);
}

void
log_msg_tags_init(void)
{
  log_tags_register_predefined_tag("message.utf8_sanitized", LM_T_MSG_UTF8_SANITIZED);

  log_tags_register_predefined_tag("syslog.invalid_pri", LM_T_SYSLOG_INVALID_PRI);
  log_tags_register_predefined_tag("syslog.missing_pri", LM_T_SYSLOG_MISSING_PRI);
  log_tags_register_predefined_tag("syslog.missing_timestamp", LM_T_SYSLOG_MISSING_TIMESTAMP);
  log_tags_register_predefined_tag("syslog.invalid_hostname", LM_T_SYSLOG_INVALID_HOSTNAME);
  log_tags_register_predefined_tag("syslog.unexpected_framing", LM_T_SYSLOG_UNEXPECTED_FRAMING);
  log_tags_register_predefined_tag("syslog.rfc3164_missing_header", LM_T_SYSLOG_RFC3164_MISSING_HEADER);
  log_tags_register_predefined_tag("syslog.rfc3164_invalid_program", LM_T_SYSLOG_RFC_3164_INVALID_PROGRAM);

  log_tags_register_predefined_tag("syslog.rfc5424_missing_hostname", LM_T_SYSLOG_RFC5424_MISSING_HOSTNAME);
  log_tags_register_predefined_tag("syslog.rfc5424_missing_app_name", LM_T_SYSLOG_RFC5424_MISSING_APP_NAME);
  log_tags_register_predefined_tag("syslog.rfc5424_missing_procid", LM_T_SYSLOG_RFC5424_MISSING_PROCID);
  log_tags_register_predefined_tag("syslog.rfc5424_missing_msgid", LM_T_SYSLOG_RFC5424_MISSING_MSGID);
  log_tags_register_predefined_tag("syslog.rfc5424_missing_sdata", LM_T_SYSLOG_RFC5424_MISSING_SDATA);
  log_tags_register_predefined_tag("syslog.rfc5424_invalid_sdata", LM_T_SYSLOG_RFC5424_INVALID_SDATA);
  log_tags_register_predefined_tag("syslog.rfc5424_missing_message", LM_T_SYSLOG_RFC5424_MISSING_MESSAGE);
}

void
log_msg_registry_init(void)
{
  gint i;

  logmsg_registry = nv_registry_new(builtin_value_names, NVHANDLE_MAX_VALUE);
  nv_registry_add_alias(logmsg_registry, LM_V_MESSAGE, "MSG");
  nv_registry_add_alias(logmsg_registry, LM_V_MESSAGE, "MSGONLY");
  nv_registry_add_alias(logmsg_registry, LM_V_HOST, "FULLHOST");
  nv_registry_add_alias(logmsg_registry, LM_V_HOST_FROM, "FULLHOST_FROM");

  nv_registry_add_predefined(logmsg_registry, LM_V_RAWMSG, "RAWMSG");
  nv_registry_add_predefined(logmsg_registry, LM_V_TRANSPORT, "TRANSPORT");
  nv_registry_add_predefined(logmsg_registry, LM_V_MSGFORMAT, "MSGFORMAT");
  nv_registry_add_predefined(logmsg_registry, LM_V_FILE_NAME, "FILE_NAME");
  nv_registry_add_predefined(logmsg_registry, LM_V_PEER_IP, "PEERIP");
  nv_registry_add_predefined(logmsg_registry, LM_V_PEER_PORT, "PEERPORT");

  nv_registry_assert_next_handle(logmsg_registry, LM_V_PREDEFINED_MAX);

  for (i = 0; macros[i].name; i++)
    {
      if (nv_registry_get_handle(logmsg_registry, macros[i].name) == 0)
        {
          NVHandle handle;

          handle = nv_registry_alloc_handle(logmsg_registry, macros[i].name);
          nv_registry_set_handle_flags(logmsg_registry, handle, (macros[i].id << 8) + LM_VF_MACRO);
        }
    }

  /* register $0 - $255 in order */
  for (i = 0; i < LOGMSG_MAX_MATCHES; i++)
    {
      gchar buf[8];

      g_snprintf(buf, sizeof(buf), "%d", i);
      match_handles[i] = nv_registry_alloc_handle(logmsg_registry, buf);
      nv_registry_set_handle_flags(logmsg_registry, match_handles[i], (i << 8) + LM_VF_MATCH);
    }
}

void
log_msg_registry_deinit(void)
{
  nv_registry_free(logmsg_registry);
  logmsg_registry = NULL;
}

void
log_msg_registry_foreach(GHFunc func, gpointer user_data)
{
  nv_registry_foreach(logmsg_registry, func, user_data);
}

static void
log_msg_register_stats(void)
{
  stats_lock();
  StatsClusterKey sc_key;
  stats_cluster_logpipe_key_legacy_set(&sc_key, SCS_GLOBAL, "msg_clones", NULL );
  stats_register_counter(0, &sc_key, SC_TYPE_PROCESSED, &count_msg_clones);

  stats_cluster_logpipe_key_legacy_set(&sc_key, SCS_GLOBAL, "payload_reallocs", NULL );
  stats_register_counter(0, &sc_key, SC_TYPE_PROCESSED, &count_payload_reallocs);

  stats_cluster_logpipe_key_legacy_set(&sc_key, SCS_GLOBAL, "sdata_updates", NULL );
  stats_register_counter(0, &sc_key, SC_TYPE_PROCESSED, &count_sdata_updates);

  stats_cluster_single_key_set(&sc_key, "events_allocated_bytes", NULL, 0);
  stats_cluster_single_key_add_legacy_alias(&sc_key, SCS_GLOBAL, "msg_allocated_bytes", NULL);
  stats_register_counter(1, &sc_key, SC_TYPE_SINGLE_VALUE, &count_allocated_bytes);
  stats_unlock();
}

void
log_msg_global_init(void)
{
  log_msg_registry_init();
  log_tags_global_init();
  log_msg_tags_init();

  /* NOTE: we always initialize counters as they are on stats-level(0),
   * however we need to defer that as the stats subsystem may not be
   * operational yet */
  register_application_hook(AH_RUNNING, (ApplicationHookFunc) log_msg_register_stats, NULL, AHM_RUN_ONCE);
}


const gchar *
log_msg_get_handle_name(NVHandle handle, gssize *length)
{
  return nv_registry_get_handle_name(logmsg_registry, handle, length);
}

void
log_msg_global_deinit(void)
{
  log_tags_global_deinit();
  log_msg_registry_deinit();
}

gint
log_msg_lookup_time_stamp_name(const gchar *name)
{
  if (strcmp(name, "stamp") == 0)
    return LM_TS_STAMP;
  else if (strcmp(name, "recvd") == 0)
    return LM_TS_RECVD;
  return -1;
}

gssize
log_msg_get_size(LogMessage *self)
{
  if (!self)
    return 0;

  return
    sizeof(LogMessage) + // msg.static fields
    + self->alloc_sdata * sizeof(self->sdata[0]) +
    g_sockaddr_len(self->saddr) + g_sockaddr_len(self->daddr) +
    ((self->num_tags) ? sizeof(self->tags[0]) * self->num_tags : 0) +
    nv_table_get_memory_consumption(self->payload); // msg.payload (nvtable)
}

#ifdef __linux__

const gchar *
__log_msg_get_value(const LogMessage *self, NVHandle handle, gssize *value_len)
__attribute__((alias("log_msg_get_value")));

const gchar *
__log_msg_get_value_by_name(const LogMessage *self, const gchar *name, gssize *value_len)
__attribute__((alias("log_msg_get_value_by_name")));

void
__log_msg_set_value_by_name(LogMessage *self, const gchar *name, const gchar *value, gssize length)
__attribute__((alias("log_msg_set_value_by_name")));

#endif

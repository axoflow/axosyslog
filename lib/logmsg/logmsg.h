/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2013 Balázs Scheidler
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

#ifndef LOGMSG_H_INCLUDED
#define LOGMSG_H_INCLUDED

#include "syslog-ng.h"
#include "gsockaddr.h"
#include "atomic.h"
#include "serialize.h"
#include "timeutils/unixtime.h"
#include "logmsg/nvtable.h"
#include "logmsg/tags.h"
#include "messages.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <iv_list.h>

typedef enum
{
  AT_UNDEFINED,
  AT_PROCESSED,
  AT_ABORTED,
  AT_SUSPENDED
} AckType;

#define IS_ACK_ABORTED(x) ((x) == AT_ABORTED ? 1 : 0)

#define IS_ABORTFLAG_ON(x) ((x) == 1 ? TRUE : FALSE)

#define IS_ACK_SUSPENDED(x) ((x) == AT_SUSPENDED ? 1 : 0)

#define IS_SUSPENDFLAG_ON(x) ((x) == 1 ? TRUE : FALSE)

#define STRICT_ROUND_TO_NEXT_EIGHT(x)  ((x + 8) & ~7)

typedef struct _LogPathOptions LogPathOptions;

typedef void (*LMAckFunc)(LogMessage *lm, AckType ack_type);

#define LOGMSG_MAX_MATCHES 256

typedef enum
{
  LM_TS_STAMP = 0,
  LM_TS_RECVD = 1,
  LM_TS_PROCESSED = 2,
  LM_TS_MAX
} LogMessageTimeStamp;

/* builtin values */
enum
{
  LM_V_NONE,
  LM_V_HOST,
  LM_V_HOST_FROM,
  LM_V_MESSAGE,
  LM_V_PROGRAM,
  LM_V_PID,
  LM_V_MSGID,
  LM_V_SOURCE,
  LM_V_LEGACY_MSGHDR,

  /* NOTE: this is used as the number of "statically" allocated elements in
   * an NVTable.  NVTable may impose restrictions on this value (for
   * instance had to be an even number earlier).  So be sure to validate
   * whether LM_V_MAX would fit NVTable if you add further enums here.
   */

  LM_V_MAX,

  /* these are dynamic values but with a predefined handle */
  LM_V_RAWMSG,
  LM_V_TRANSPORT,
  LM_V_MSGFORMAT,
  LM_V_FILE_NAME,
  LM_V_PEER_IP,
  LM_V_PEER_PORT,

  LM_V_PREDEFINED_MAX,
};

enum
{
  /* means that the message is not valid utf8 */
  LM_T_MSG_UTF8_SANITIZED,
  /* missing <pri> value */
  LM_T_SYSLOG_MISSING_PRI,
  /* invalid <pri> value */
  LM_T_SYSLOG_INVALID_PRI,
  /* no timestamp present in the original message */
  LM_T_SYSLOG_MISSING_TIMESTAMP,
  /* hostname field does not seem valid, check-hostname(yes) failed */
  LM_T_SYSLOG_INVALID_HOSTNAME,
  /* we seem to have found an octet count in front of the message */
  LM_T_SYSLOG_UNEXPECTED_FRAMING,
  /* no date & host information in the syslog message */
  LM_T_SYSLOG_RFC3164_MISSING_HEADER,
  /* hostname field missing */
  LM_T_SYSLOG_RFC5424_MISSING_HOSTNAME,
  /* program field missing */
  LM_T_SYSLOG_RFC5424_MISSING_APP_NAME,
  /* pid field missing */
  LM_T_SYSLOG_RFC5424_MISSING_PROCID,
  /* msgid field missing */
  LM_T_SYSLOG_RFC5424_MISSING_MSGID,
  /* sdata field missing */
  LM_T_SYSLOG_RFC5424_MISSING_SDATA,
  /* invalid SDATA */
  LM_T_SYSLOG_RFC5424_INVALID_SDATA,
  /* sdata field missing */
  LM_T_SYSLOG_RFC5424_MISSING_MESSAGE,
  /* message field missing */
  LM_T_SYSLOG_MISSING_MESSAGE,
  /* invalid program name */
  LM_T_SYSLOG_RFC_3164_INVALID_PROGRAM,

  LM_T_PREDEFINED_MAX,
};

enum
{
  LM_VF_SDATA = 0x0001,
  LM_VF_MATCH = 0x0002,
  LM_VF_MACRO = 0x0004,
};

enum
{
  /* these flags also matter when the message is serialized */
  LF_OLD_UNPARSED      = 0x0001,
  /* message payload is guaranteed to be valid utf8 */
  LF_UTF8              = 0x0001,
  /* message was generated from within syslog-ng, doesn't matter if it came from the internal() source */
  LF_INTERNAL          = 0x0002,
  /* message was received on a local transport, e.g. it was generated on the local machine */
  LF_LOCAL             = 0x0004,
  /* message is a MARK mode */
  LF_MARK              = 0x0008,

  /* state flags that only matter during syslog-ng runtime and never
   * when a message is serialized */
  LF_STATE_MASK        = 0xFFF0,
  LF_STATE_OWN_PAYLOAD = 0x0010,
  LF_STATE_OWN_SADDR   = 0x0020,
  LF_STATE_OWN_DADDR   = 0x0040,
  LF_STATE_OWN_TAGS    = 0x0080,
  LF_STATE_OWN_SDATA   = 0x0100,
  LF_STATE_OWN_MASK    = 0x01F0,

  /* part of the state that is kept across clones */
  LF_STATE_CLONED_MASK = 0xFE00,
  LF_STATE_TRACING     = 0x0200,

  LF_CHAINED_HOSTNAME  = 0x00010000,

  /* NOTE: this flag is now unused.  The original intent was to save whether
   * LEGACY_MSGHDR was saved by the parser code.  Now we simply check
   * whether the length of ${LEGACY_MSGHDR} is non-zero.  This used to be a
   * slow operation (when name-value pairs were stored in a hashtable), now
   * it is much faster.  Also, this makes it possible to reproduce a message
   * entirely based on name-value pairs.  Without this change, even if
   * LEGACY_MSGHDR was transferred (e.g.  ewmm), the other side couldn't
   * reproduce the original message, as this flag was not transferred.
   *
   * The flag remains here for documentation, and also because it is serialized in disk-buffers
   */
  __UNUSED_LF_LEGACY_MSGHDR    = 0x00020000,
};

typedef NVType LogMessageValueType;
enum _LogMessageValueType
{
  /* Everything is represented as a string, formatted in a type specific
   * automatically parseable format.
   *
   * Please note that these values are part of the serialized LogMessage
   * format, so changing these would cause incompatibilities in type
   * recognition. Add new types at the end.
   */
  LM_VT_STRING = 0,
  LM_VT_JSON = 1,
  LM_VT_BOOLEAN = 2,
  __COMPAT_LM_VT_INT32 = 3,
  __COMPAT_LM_VT_INT64 = 4,
  LM_VT_INTEGER = 4,  /* equals to LM_VT_INT64 */
  LM_VT_DOUBLE = 5,
  LM_VT_DATETIME = 6,
  LM_VT_LIST = 7,
  LM_VT_NULL = 8,
  LM_VT_BYTES = 9,
  LM_VT_PROTOBUF = 10,

  /* extremal value to indicate "unset" state.
   *
   * NOTE: THIS IS NOT THE DEFAULT for actual values even if type is
   * unspecified, those cases default to LM_VT_STRING.
   */
  LM_VT_NONE = 255
};

const gchar *log_msg_value_type_to_str(LogMessageValueType self);
gboolean log_msg_value_type_from_str(const gchar *in_str, LogMessageValueType *out_type);

typedef struct _LogMessageQueueNode
{
  struct iv_list_head list;
  LogMessage *msg;
  guint ack_needed:1, embedded:1, flow_control_requested:1;
} LogMessageQueueNode;


/* NOTE: the members are ordered according to the presumed use frequency.
 * The structure itself is 2 cachelines, the border is right after the "msg"
 * member */
struct _LogMessage
{
  /* if you change any of the fields here, be sure to adjust
   * log_msg_clone_cow() as well to initialize fields properly */

  /* some of the fields in this struct are shared in copy-on-write
   * scenarios, tracking of those fields are done using the "flags" member,
   * see the LF_STATE_OWN_* flags
   */

  /* ack_and_ref_and_abort_and_suspended is a 32 bit integer that is accessed in an atomic way.
   * The upper half contains the ACK count (and the abort flag), the lower half
   * the REF count.  It is not a GAtomicCounter as due to ref/ack caching it has
   * a lot of magic behind its implementation.  See the logmsg.c file, around
   * log_msg_ref/unref.
   */

  gint ack_and_ref_and_abort_and_suspended;
  guint32 flags;

  NVTable *payload;
  LogMessage *original;
  gulong *tags;
  NVHandle *sdata;

  /* this member is incremented for any write operation and it can also
   * overflow, so only track it for changes and assume that 2^16 operations
   * would suffice between two checks */
  guint16 generation;
  guint16 pri;
  guint8 initial_parse:1,
         recursed:1,

         /* NOTE: proto is just 6 bits wide, but with that it fills a hole
          * not taking any tolls on the structure size.  Realistically, we'd
          * be storing IPPROTO_UDP and TCP in there, which fits the 6 bits.
          * This is closely related to saddr/daddr and indicates the IP
          * protocol that was used to deliver the datagram carrying this
          * LogMessage.  */

         proto:6;
  /* number of capture groups retrieved from a regexp match (e.g. $1, $2, ...) */
  guint8 num_matches;

  /* number of bits in the "tags" array, if less than 64, all such bits are
   * stored in the "tags" pointer, otherwise it points to an allocated bit
   * array */
  guint8 num_tags;
  /* number of items allocated in the "sdata" array */
  guint8 alloc_sdata;
  /* number of items stored in the "sdata" array */
  guint8 num_sdata;

  /* number of nodes pre-allocated as a part of LogMessage at the tail end of the structure */
  guint8 num_nodes;

  /* the next available node */
  guint8 cur_node;
  /* is this message currently read only, used to track when we need to copy-on-write */
  guint8 write_protected;
  /* identifier of the source host */
  guint32 host_id;
  /* unique message identifier (upon receipt) */
  guint64 rcptid;

  /* number of bytes in the received message */
  guint32 recvd_rawmsg_size;

  /* allocated bytes in LogMessage, is limited to 32 bits as it's highly
   * unlikely that we would ever need more than 4GB for a single message
   * including overhead */
  guint32 allocated_bytes;

  AckRecord *ack_record;
  LMAckFunc ack_func;

  GSockAddr *saddr;
  GSockAddr *daddr;

  UnixTime timestamps[LM_TS_MAX];

  /* preallocated LogQueueNodes used to insert this message into a LogQueue */
  LogMessageQueueNode nodes[0];

  /* a preallocated space for the initial NVTable (payload) may follow */
};

extern NVRegistry *logmsg_registry;
extern const char logmsg_sd_prefix[];
extern const gint logmsg_sd_prefix_len;
extern gint logmsg_node_max;

LogMessage *log_msg_ref(LogMessage *m);
void log_msg_unref(LogMessage *m);

static inline void
log_msg_write_protect(LogMessage *self)
{
  self->write_protected = TRUE;
}

static inline gboolean
log_msg_is_write_protected(const LogMessage *self)
{
  return self->write_protected;
}

LogMessage *log_msg_clone_cow(LogMessage *msg, const LogPathOptions *path_options);

static inline LogMessage *
log_msg_make_writable(LogMessage **pself, const LogPathOptions *path_options)
{
  if (log_msg_is_write_protected(*pself))
    {
      LogMessage *new_msg;

      new_msg = log_msg_clone_cow(*pself, path_options);
      log_msg_unref(*pself);
      *pself = new_msg;
    }
  return *pself;
}

gboolean log_msg_write(LogMessage *self, SerializeArchive *sa);
gboolean log_msg_read(LogMessage *self, SerializeArchive *sa);

/* generic values that encapsulate log message fields, dynamic values and structured data */
NVHandle log_msg_get_value_handle(const gchar *value_name);
gboolean log_msg_is_value_name_valid(const gchar *value);
const gchar *log_msg_get_handle_name(NVHandle handle, gssize *length);

static inline gboolean
log_msg_is_handle_macro(NVHandle handle)
{
  guint16 flags;

  flags = nv_registry_get_handle_flags(logmsg_registry, handle);
  return !!(flags & LM_VF_MACRO);
}

static inline gboolean
log_msg_is_handle_sdata(NVHandle handle)
{
  guint16 flags;

  flags = nv_registry_get_handle_flags(logmsg_registry, handle);
  return !!(flags & LM_VF_SDATA);
}

static inline gboolean
log_msg_is_handle_match(NVHandle handle)
{
  guint16 flags;

  flags = nv_registry_get_handle_flags(logmsg_registry, handle);
  return !!(flags & LM_VF_MATCH);
}

static inline gboolean
log_msg_is_handle_referencable_from_an_indirect_value(NVHandle handle)
{
  if (handle == LM_V_NONE)
    return FALSE;

  /* macro values should not be referenced as they are dynamic, store the actual value instead */
  if (log_msg_is_handle_macro(handle))
    return FALSE;

  /* matches are pretty temporary, so we should not reference them, as the
   * next matching operation would overwrite them anyway */

  if (log_msg_is_handle_match(handle))
    return FALSE;

  return TRUE;
}

static inline gboolean
log_msg_is_handle_settable_with_an_indirect_value(NVHandle handle)
{
  return (handle >= LM_V_MAX);
}

const gchar *log_msg_get_macro_value(const LogMessage *self, gint id, gssize *value_len, LogMessageValueType *type);
const gchar *log_msg_get_match_with_type(const LogMessage *self, gint index_,
                                         gssize *value_len, LogMessageValueType *type);
const gchar *log_msg_get_match_if_set_with_type(const LogMessage *self, gint index_,
                                                gssize *value_len, LogMessageValueType *type);



static inline const gchar *
log_msg_get_value_if_set_with_type(const LogMessage *self, NVHandle handle,
                                   gssize *value_len,
                                   LogMessageValueType *type)
{
  guint16 flags;

  flags = nv_registry_get_handle_flags(logmsg_registry, handle);
  if (G_UNLIKELY((flags & LM_VF_MACRO)))
    return log_msg_get_macro_value(self, flags >> 8, value_len, type);
  else
    return nv_table_get_value(self->payload, handle, value_len, type);
}

static inline gboolean
log_msg_is_value_set(const LogMessage *self, NVHandle handle)
{
  return nv_table_is_value_set(self->payload, handle);
}

static inline const gchar *
log_msg_get_value_with_type(const LogMessage *self, NVHandle handle, gssize *value_len, LogMessageValueType *type)
{
  const gchar *result = log_msg_get_value_if_set_with_type(self, handle, value_len, type);

  if (result)
    return result;
  if (type)
    *type = LM_VT_NULL;
  if (value_len)
    *value_len = 0;
  return "";
}

static inline const gchar *
log_msg_get_value(const LogMessage *self, NVHandle handle, gssize *value_len)
{
  return log_msg_get_value_with_type(self, handle, value_len, NULL);
}

static inline const gchar *
log_msg_get_value_if_set(const LogMessage *self, NVHandle handle, gssize *value_len)
{
  return log_msg_get_value_if_set_with_type(self, handle, value_len, NULL);
}

static inline const gchar *
log_msg_get_value_by_name(const LogMessage *self, const gchar *name, gssize *value_len)
{
  NVHandle handle = log_msg_get_value_handle(name);
  return log_msg_get_value(self, handle, value_len);
}

static inline const gchar *
log_msg_get_value_by_name_with_type(const LogMessage *self,
                                    const gchar *name, gssize *value_len,
                                    LogMessageValueType *type)
{
  NVHandle handle = log_msg_get_value_handle(name);
  return log_msg_get_value_with_type(self, handle, value_len, type);
}

static inline const gchar *
log_msg_get_value_name(NVHandle handle, gssize *name_len)
{
  return nv_registry_get_handle_name(logmsg_registry, handle, name_len);
}

typedef gboolean (*LogMessageTagsForeachFunc)(const LogMessage *self, LogTagId tag_id, const gchar *name,
                                              gpointer user_data);

void log_msg_set_value(LogMessage *self, NVHandle handle, const gchar *new_value, gssize length);
void log_msg_set_value_with_type(LogMessage *self, NVHandle handle,
                                 const gchar *value, gssize value_len,
                                 LogMessageValueType type);

void log_msg_set_value_indirect(LogMessage *self, NVHandle handle, NVHandle ref_handle,
                                guint16 ofs, guint16 len);
void log_msg_set_value_indirect_with_type(LogMessage *self, NVHandle handle, NVHandle ref_handle,
                                          guint16 ofs, guint16 len, LogMessageValueType type);
void log_msg_unset_value(LogMessage *self, NVHandle handle);
void log_msg_unset_value_by_name(LogMessage *self, const gchar *name);
gboolean log_msg_values_foreach(const LogMessage *self, NVTableForeachFunc func, gpointer user_data);
NVHandle log_msg_get_match_handle(gint index_);
gint log_msg_get_match_index(NVHandle handle);
void log_msg_set_match(LogMessage *self, gint index, const gchar *value, gssize value_len);
void log_msg_set_match_with_type(LogMessage *self, gint index,
                                 const gchar *value, gssize value_len,
                                 LogMessageValueType type);
void log_msg_set_match_indirect(LogMessage *self, gint index, NVHandle ref_handle, guint16 ofs, guint16 len);
void log_msg_set_match_indirect_with_type(LogMessage *self, gint index, NVHandle ref_handle,
                                          guint16 ofs, guint16 len, LogMessageValueType type);
void log_msg_unset_match(LogMessage *self, gint index_);
const gchar *log_msg_get_match_with_type(const LogMessage *self, gint index_, gssize *value_len,
                                         LogMessageValueType *type);
const gchar *log_msg_get_match(const LogMessage *self, gint index_, gssize *value_len);
void log_msg_clear_matches(LogMessage *self);
void log_msg_truncate_matches(LogMessage *self, gint n);

static inline void
log_msg_set_value_by_name_with_type(LogMessage *self,
                                    const gchar *name, const gchar *value, gssize length,
                                    LogMessageValueType type)
{
  NVHandle handle = log_msg_get_value_handle(name);
  log_msg_set_value_with_type(self, handle, value, length, type);
}

static inline void
log_msg_set_value_by_name(LogMessage *self, const gchar *name, const gchar *value, gssize length)
{
  log_msg_set_value_by_name_with_type(self, name, value, length, LM_VT_STRING);
}

static inline void
log_msg_set_value_to_string(LogMessage *self, NVHandle handle, const gchar *literal_string)
{
  log_msg_set_value(self, handle, literal_string, strlen(literal_string));
}

void log_msg_rename_value(LogMessage *self, NVHandle from, NVHandle to);

void log_msg_append_format_sdata(const LogMessage *self, GString *result, guint32 seq_num);
void log_msg_format_sdata(const LogMessage *self, GString *result, guint32 seq_num);
void log_msg_clear_sdata(LogMessage *self);

void log_msg_set_tag_by_id_onoff(LogMessage *self, LogTagId id, gboolean on);
void log_msg_set_tag_by_id(LogMessage *self, LogTagId id);
void log_msg_set_tag_by_name(LogMessage *self, const gchar *name);
void log_msg_clear_tag_by_id(LogMessage *self, LogTagId id);
void log_msg_clear_tag_by_name(LogMessage *self, const gchar *name);
gboolean log_msg_is_tag_by_id(LogMessage *self, LogTagId id);
gboolean log_msg_is_tag_by_name(LogMessage *self, const gchar *name);
void log_msg_tags_foreach(const LogMessage *self, LogMessageTagsForeachFunc callback, gpointer user_data);
void log_msg_format_tags(const LogMessage *self, GString *result, gboolean include_localtags);
void log_msg_format_matches(const LogMessage *self, GString *result);


static inline void
log_msg_set_recvd_rawmsg_size(LogMessage *self, guint32 size)
{
  self->recvd_rawmsg_size = size;
}

void log_msg_set_saddr(LogMessage *self, GSockAddr *saddr);
void log_msg_set_saddr_ref(LogMessage *self, GSockAddr *saddr);
void log_msg_set_daddr(LogMessage *self, GSockAddr *daddr);
void log_msg_set_daddr_ref(LogMessage *self, GSockAddr *daddr);


LogMessageQueueNode *log_msg_alloc_queue_node(LogMessage *msg, const LogPathOptions *path_options);
LogMessageQueueNode *log_msg_alloc_dynamic_queue_node(LogMessage *msg, const LogPathOptions *path_options);
void log_msg_free_queue_node(LogMessageQueueNode *node);

void log_msg_clear(LogMessage *self);
void log_msg_merge_context(LogMessage *self, LogMessage **context, gsize context_len);

LogMessage *log_msg_sized_new(gsize payload_size);
LogMessage *log_msg_new_mark(void);
LogMessage *log_msg_new_internal(gint prio, const gchar *msg);
LogMessage *log_msg_new_empty(void);
LogMessage *log_msg_new_local(void);

void log_msg_add_ack(LogMessage *msg, const LogPathOptions *path_options);
void log_msg_ack(LogMessage *msg, const LogPathOptions *path_options, AckType ack_type);
void log_msg_drop(LogMessage *msg, const LogPathOptions *path_options, AckType ack_type);
const LogPathOptions *log_msg_break_ack(LogMessage *msg, const LogPathOptions *path_options,
                                        LogPathOptions *local_path_options);

void log_msg_refcache_start_producer(LogMessage *self);
void log_msg_refcache_start_consumer(LogMessage *self, const LogPathOptions *path_options);
void log_msg_refcache_stop(void);

void log_msg_registry_init(void);
void log_msg_registry_deinit(void);
void log_msg_global_init(void);
void log_msg_global_deinit(void);
void log_msg_stats_global_init(void);
void log_msg_registry_foreach(GHFunc func, gpointer user_data);

gint log_msg_lookup_time_stamp_name(const gchar *name);

gssize log_msg_get_size(LogMessage *self);

#define evt_tag_msg_reference(msg)             \
    evt_tag_printf("msg", "%p", (msg)),        \
    evt_tag_printf("rcptid", "%" G_GUINT64_FORMAT, (msg)->rcptid)

static inline EVTTAG *
evt_tag_msg_value(const gchar *name, LogMessage *msg, NVHandle value_handle)
{
  gssize value_len;
  const gchar *value = log_msg_get_value(msg, value_handle, &value_len);

  return evt_tag_mem(name, value, value_len);
}

static inline EVTTAG *
evt_tag_msg_value_name(const gchar *name, NVHandle value_handle)
{
  const gchar *value_name = log_msg_get_value_name(value_handle, NULL);

  return evt_tag_str(name, value_name);
}

#endif

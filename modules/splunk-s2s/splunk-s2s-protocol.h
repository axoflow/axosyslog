/*
 * Copyright (c) 2026 Adam Kiss
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

#ifndef SPLUNK_S2S_PROTOCOL_INCLUDED
#define SPLUNK_S2S_PROTOCOL_INCLUDED

#include "syslog-ng.h"

/*
 * Wire-format codec for Splunk's S2S ("cooked mode") protocol, as spoken
 * by Splunk forwarders towards an indexer.  A connection is layered as:
 *
 *   header1     fixed 400-byte block sent once: magic + identifier + mgmt port
 *   v3 frames   BE-u32 length-prefixed key/value pairs; capability handshake
 *   v4 packets  tagged, varint-coded; entered once the sender advertises v4=1
 *
 * All varints are ULEB128.
 */

#define SPLUNK_S2S_MAGIC_V3 "--splunk-cooked-mode-v3--"
#define SPLUNK_S2S_MAGIC_V2 "--splunk-cooked-mode-v2--"
#define SPLUNK_S2S_MAGIC_V2_COMPRESSED "--splunk-cooked-mode-v2--:C"
#define SPLUNK_S2S_MAGIC_V0 "--splunk-cooked-mode--"
#define SPLUNK_S2S_HEADER1_SIZE 0x190
/* each header1 slot is a NUL-terminated string, one byte below the slot width */
#define SPLUNK_S2S_HEADER1_MAX_IDENTIFIER_LEN 0xff
#define SPLUNK_S2S_HEADER1_MAX_MGMT_PORT_LEN 0xf

#define SPLUNK_S2S_CAPABILITIES_SIGNATURE "ack=0;compression=0"
#define SPLUNK_S2S_CAPABILITIES_SIGNATURE_ACK "ack=1;compression=0"
#define SPLUNK_S2S_CAPABILITIES_V4 "cli_can_rcv_hb=1;compression=0;pl=7;request_certificate=1;v4=1"

/* the capability grant a Splunk 10.2.2 indexer replies with */
#define SPLUNK_S2S_CAPABILITIES_SERVER_HELLO \
  "cap_response=success;cap_flush_key=true;idx_can_send_hb=true;idx_can_recv_token=true;" \
  "request_certificate=true;v4=true;channel_limit=300;pl=7"

typedef enum
{
  SPLUNK_S2S_PKT_INIT = 0xff,
  SPLUNK_S2S_PKT_OPEN_CHANNEL = 0xfe,
  SPLUNK_S2S_PKT_CLOSE_CHANNEL = 0xfd,
  SPLUNK_S2S_PKT_EVENT = 0xfc,

  /* reverse direction (indexer to forwarder), sent when the forwarder
   * requested acknowledgements with ack=1 */
  SPLUNK_S2S_PKT_ACK_ONE = 0xfb,
  SPLUNK_S2S_PKT_ACK_RANGE = 0xfa,

  SPLUNK_S2S_PKT_START_ZLIB = 0xf9,
  SPLUNK_S2S_PKT_CLONE_CHANNEL = 0xf8,
} SplunkS2SPacketId;

/* the full/short header event_flags values forwarders emit;
 * bits 0x2|0x8 gate the positional header form */
#define SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER 0x27f
#define SPLUNK_S2S_EVENT_FLAGS_SHORT_HEADER 0x235
#define SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER_MASK (0x2 | 0x8)

/* each bit independently gates its varint(s) in the positional event header */
#define SPLUNK_S2S_EVENT_FLAG_STREAM_POS 0x2
#define SPLUNK_S2S_EVENT_FLAG_EVENT_ID 0x4
#define SPLUNK_S2S_EVENT_FLAG_TIMESTAMP 0x8
/* line merging already happened on the forwarder, the payload is one event */
#define SPLUNK_S2S_EVENT_FLAG_AGGREGATED 0x40
#define SPLUNK_S2S_EVENT_FLAG_RAW_SECTION 0x10000

typedef struct _SplunkS2SStringPair
{
  const gchar *key;
  const gchar *value;
} SplunkS2SStringPair;

typedef enum
{
  SPLUNK_S2S_NAME_INLINE_STR = 0,
  SPLUNK_S2S_NAME_CODE_DYNAMIC = 1,
  SPLUNK_S2S_NAME_CODE_PREDEFINED = 2,
  SPLUNK_S2S_NAME_META_INLINE_STR = 3,
} SplunkS2SNameType;

typedef enum
{
  SPLUNK_S2S_VALUE_NUMBER = 0,
  SPLUNK_S2S_VALUE_STR = 1,
  SPLUNK_S2S_VALUE_CODE_PREDEFINED = 2,
  SPLUNK_S2S_VALUE_RAW_OFFSET_LEN = 3,
} SplunkS2SValueType;

/* field-level flag bits (the type descriptor varint above bit 4) */

/* on a CODE_PREDEFINED value slot the value is an (offset, length) reference
 * into the payload instead of a table code */
#define SPLUNK_S2S_FIELD_FLAG_CODE_IS_OFFSET_LEN 0x1
/* two extra varints follow the value (per-sample metric references) */
#define SPLUNK_S2S_FIELD_FLAG_HAS_EXTRAS 0x8

typedef struct _SplunkS2SEventField
{
  const gchar *name;
  SplunkS2SValueType value_type;
  guint64 number_value;
  const gchar *str_value;
  gsize str_value_len;
} SplunkS2SEventField;

void splunk_s2s_write_varint(GString *out, guint64 value);

gboolean splunk_s2s_format_header1(GString *out, const gchar *identifier, const gchar *mgmt_port);
void splunk_s2s_format_v3_frame(GString *out, const SplunkS2SStringPair *pairs, gsize n_pairs);
void splunk_s2s_format_v3_signature_frame(GString *out, const gchar *capabilities);
void splunk_s2s_format_v3_control_frame(GString *out, const gchar *capabilities);
void splunk_s2s_format_v3_forwarder_info_frame(GString *out, const gchar *forwarder_info, const gchar *guid,
                                               guint64 timestamp);
void splunk_s2s_format_open_channel(GString *out, guint64 channel_id, const gchar *source, const gchar *host,
                                    const gchar *sourcetype);
void splunk_s2s_format_event(GString *out, guint64 channel_id, guint64 event_flags, guint64 timestamp,
                             guint64 event_id, const SplunkS2SEventField *fields, gsize n_fields,
                             const gchar *raw, gsize raw_len);
void splunk_s2s_format_close_channel(GString *out, guint64 channel_id);
void splunk_s2s_format_ack(GString *out, guint64 lo, guint64 hi);

gboolean splunk_s2s_parse_v3_frame_len(const guchar *buf, gsize buf_len, guint32 *frame_len);

/*
 * Incremental parsers for the forwarder-to-indexer direction.  All of them
 * only advance *pos when a complete element was consumed: on
 * SPLUNK_S2S_PARSE_MORE the caller keeps the buffer and retries with more
 * data, on SPLUNK_S2S_PARSE_ERROR *error names the confirmed protocol
 * violation.  Returned pointers reference the input buffer.
 */

typedef enum
{
  SPLUNK_S2S_PARSE_OK,
  SPLUNK_S2S_PARSE_MORE,
  SPLUNK_S2S_PARSE_ERROR,
} SplunkS2SParseResult;

/* SPLUNK_S2S_PARSE_ERROR means the varint ran past the 64-bit ceiling,
 * which cannot be valid input and must not be waited out */
SplunkS2SParseResult splunk_s2s_parse_varint(const guchar *buf, gsize buf_len, gsize *pos, guint64 *value);

gboolean splunk_s2s_parse_header1(const guchar *buf, const gchar **identifier, const gchar **mgmt_port);

SplunkS2SParseResult splunk_s2s_parse_varint_str(const guchar *buf, gsize buf_len, gsize *pos,
                                                 const gchar **data, gsize *data_len);

typedef void (*SplunkS2SV3PairFunc)(const gchar *key, gsize key_len, const gchar *value, gsize value_len,
                                    gpointer user_data);
gboolean splunk_s2s_v3_frame_foreach_pair(const guchar *frame, gsize frame_len, SplunkS2SV3PairFunc func,
                                          gpointer user_data);

#define SPLUNK_S2S_MAX_CHANNEL_HEADERS 8

typedef struct _SplunkS2SParsedChannel
{
  guint64 channel_id;
  guint64 template_channel_id;
  const gchar *headers[SPLUNK_S2S_MAX_CHANNEL_HEADERS];
  gsize header_lens[SPLUNK_S2S_MAX_CHANNEL_HEADERS];
  gsize n_headers;
} SplunkS2SParsedChannel;

SplunkS2SParseResult splunk_s2s_parse_open_channel(const guchar *buf, gsize buf_len, gsize *pos,
                                                   SplunkS2SParsedChannel *channel, const gchar **error);
SplunkS2SParseResult splunk_s2s_parse_clone_channel(const guchar *buf, gsize buf_len, gsize *pos,
                                                    SplunkS2SParsedChannel *channel, const gchar **error);

typedef struct _SplunkS2SParsedEvent
{
  guint64 channel_id;
  guint64 flags;
  guint64 event_id;
  guint64 timestamp;

  const gchar *raw;
  gsize raw_len;

  /* per-event metadata overrides, NULL when the event did not carry them */
  const gchar *index;
  gsize index_len;
  const gchar *source;
  gsize source_len;
  const gchar *sourcetype;
  gsize sourcetype_len;
  const gchar *host;
  gsize host_len;
} SplunkS2SParsedEvent;

void splunk_s2s_strip_meta_prefix(const gchar **value, gsize *len);

SplunkS2SParseResult splunk_s2s_parse_event(const guchar *buf, gsize buf_len, gsize *pos,
                                            SplunkS2SParsedEvent *event, const gchar **error);

#endif

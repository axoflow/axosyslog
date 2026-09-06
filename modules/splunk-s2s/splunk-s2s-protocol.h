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
 * Wire-format encoder for Splunk's S2S ("cooked mode") protocol, as spoken
 * by Splunk forwarders towards an indexer.  A connection is layered as:
 *
 *   header1     fixed 400-byte block sent once: magic + identifier + mgmt port
 *   v3 frames   BE-u32 length-prefixed key/value pairs; capability handshake
 *   v4 packets  tagged, varint-coded; entered once the sender advertises v4=1
 *
 * All varints are ULEB128.
 */

#define SPLUNK_S2S_MAGIC_V3 "--splunk-cooked-mode-v3--"
#define SPLUNK_S2S_HEADER1_SIZE 0x190
/* each header1 slot is a NUL-terminated string, one byte below the slot width */
#define SPLUNK_S2S_HEADER1_MAX_IDENTIFIER_LEN 0xff
#define SPLUNK_S2S_HEADER1_MAX_MGMT_PORT_LEN 0xf

#define SPLUNK_S2S_CAPABILITIES_SIGNATURE "ack=0;compression=0"
#define SPLUNK_S2S_CAPABILITIES_V4 "cli_can_rcv_hb=1;compression=0;pl=7;request_certificate=1;v4=1"

typedef enum
{
  SPLUNK_S2S_PKT_INIT = 0xff,
  SPLUNK_S2S_PKT_OPEN_CHANNEL = 0xfe,
  SPLUNK_S2S_PKT_CLOSE_CHANNEL = 0xfd,
  SPLUNK_S2S_PKT_EVENT = 0xfc,
} SplunkS2SPacketId;

/* the full/short header event_flags values forwarders emit;
 * bits 0x2|0x8 gate the positional header form */
#define SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER 0x27f
#define SPLUNK_S2S_EVENT_FLAGS_SHORT_HEADER 0x235
#define SPLUNK_S2S_EVENT_FLAGS_FULL_HEADER_MASK (0x2 | 0x8)

typedef struct _SplunkS2SStringPair
{
  const gchar *key;
  const gchar *value;
} SplunkS2SStringPair;

typedef enum
{
  SPLUNK_S2S_VALUE_NUMBER = 0,
  SPLUNK_S2S_VALUE_STR = 1,
} SplunkS2SValueType;

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
void splunk_s2s_format_v3_signature_frame(GString *out);
void splunk_s2s_format_v3_forwarder_info_frame(GString *out, const gchar *forwarder_info, const gchar *guid,
                                               guint64 timestamp);
void splunk_s2s_format_open_channel(GString *out, guint64 channel_id, const gchar *source, const gchar *host,
                                    const gchar *sourcetype);
void splunk_s2s_format_event(GString *out, guint64 channel_id, guint64 event_flags, guint64 timestamp,
                             const SplunkS2SEventField *fields, gsize n_fields, const gchar *raw, gsize raw_len);
void splunk_s2s_format_close_channel(GString *out, guint64 channel_id);

gboolean splunk_s2s_parse_v3_frame_len(const guchar *buf, gsize buf_len, guint32 *frame_len);

#endif

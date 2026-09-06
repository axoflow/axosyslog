#ifndef TEST_SPLUNK_S2S_PROTOCOL_VECTORS_H
#define TEST_SPLUNK_S2S_PROTOCOL_VECTORS_H

/* string literal initialized, the array carries a trailing NUL that is not
 * part of the wire image */
#define VECTOR_LEN(v) (sizeof(v) - 1)

static const guint8 vector_varint_0[] =
  "\x00";  /* varint 0 */

static const guint8 vector_varint_127[] =
  "\x7f";  /* varint 127 */

static const guint8 vector_varint_128[] =
  "\x80\x01";  /* varint 128 */

static const guint8 vector_varint_300[] =
  "\xac\x02";  /* varint 300 */

static const guint8 vector_varint_u32_max[] =
  "\xff\xff\xff\xff\x0f";  /* varint 2^32 - 1 */

static const guint8 vector_varint_u64_max[] =
  "\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01";  /* varint 2^64 - 1 */

static const guint8 vector_header1[SPLUNK_S2S_HEADER1_SIZE] =
{
  [0x000] = '-', '-', 's', 'p', 'l', 'u', 'n', 'k', '-', 'c', 'o', 'o', 'k',
            'e', 'd', '-', 'm', 'o', 'd', 'e', '-', 'v', '3', '-', '-',  /* magic */
  [0x080] = 'a', 'x', 'o', 's', 'y', 's', 'l', 'o', 'g',  /* identifier */
  [0x180] = '0',  /* mgmt port */
};

static const guint8 vector_v3_signature_frame[] =
  "\x00\x00\x00\x40"   /* frame length 64 */
  "\x00\x00\x00\x01"   /* 1 pair */
  "\x00\x00\x00\x13"   /* key length 19 (NUL counted) */
  "__s2s_capabilities" "\0"
  "\x00\x00\x00\x14"   /* value length 20 (NUL counted) */
  "ack=0;compression=0" "\0"
  "\x00\x00\x00\x00"   /* zero */
  "\x00\x00\x00\x05"   /* marker length 5 */
  "_raw" "\0";

static const guint8 vector_v3_forwarder_info_frame[] =
  "\x00\x00\x01\xd7"   /* frame length 471 */
  "\x00\x00\x00\x0c"   /* 12 pairs */
  "\x00\x00\x00\x05"   /* key length 5 (NUL counted) */
  "_raw" "\0"
  "\x00\x00\x00\x13"   /* value length 19 (NUL counted) */
  "ForwarderInfo test" "\0"
  "\x00\x00\x00\x06"   /* key length 6 (NUL counted) */
  "_done" "\0"
  "\x00\x00\x00\x06"   /* value length 6 (NUL counted) */
  "_done" "\0"
  "\x00\x00\x00\x06"   /* key length 6 (NUL counted) */
  "_time" "\0"
  "\x00\x00\x00\x0b"   /* value length 11 (NUL counted) */
  "1750000000" "\0"
  "\x00\x00\x00\x06"   /* key length 6 (NUL counted) */
  "_guid" "\0"
  "\x00\x00\x00\x0e"   /* value length 14 (NUL counted) */
  "TESTGUID-1234" "\0"
  "\x00\x00\x00\x10"   /* key length 16 (NUL counted) */
  "_MetaData:Index" "\0"
  "\x00\x00\x00\x0a"   /* value length 10 (NUL counted) */
  "_internal" "\0"
  "\x00\x00\x00\x13"   /* key length 19 (NUL counted) */
  "__s2s_capabilities" "\0"
  "\x00\x00\x00\x3f"   /* value length 63 (NUL counted) */
  "cli_can_rcv_hb=1;compression=0;pl=7;request_certificate=1;v4=1" "\0"
  "\x00\x00\x00\x14"   /* key length 20 (NUL counted) */
  "MetaData:Sourcetype" "\0"
  "\x00\x00\x00\x14"   /* value length 20 (NUL counted) */
  "sourcetype::fwdinfo" "\0"
  "\x00\x00\x00\x0f"   /* key length 15 (NUL counted) */
  "_ingLatChained" "\0"
  "\x00\x00\x00\x16"   /* value length 22 (NUL counted) */
  "{\"green\":{\"count\":0}}" "\0"
  "\x00\x00\x00\x10"   /* key length 16 (NUL counted) */
  "MetaData:Source" "\0"
  "\x00\x00\x00\x0c"   /* value length 12 (NUL counted) */
  "source::fwd" "\0"
  "\x00\x00\x00\x0e"   /* key length 14 (NUL counted) */
  "MetaData:Host" "\0"
  "\x00\x00\x00\x17"   /* value length 23 (NUL counted) */
  "host::$decideOnStartup" "\0"
  "\x00\x00\x00\x0d"   /* key length 13 (NUL counted) */
  "_ingLatColor" "\0"
  "\x00\x00\x00\x06"   /* value length 6 (NUL counted) */
  "green" "\0"
  "\x00\x00\x00\x0e"   /* key length 14 (NUL counted) */
  "__s2s_eventId" "\0"
  "\x00\x00\x00\x02"   /* value length 2 (NUL counted) */
  "0" "\0"
  "\x00\x00\x00\x00"   /* zero */
  "\x00\x00\x00\x05"   /* marker length 5 */
  "_raw" "\0";

static const guint8 vector_open_channel[] =
  "\xfe"                        /* OPEN_CHANNEL */
  "\x01"                        /* channel 1 */
  "\x1a"                        /* length 26 (a phantom NUL counted) */
  "source::/var/log/messages"   /* source slot */
  "\x0f"                        /* length 15 (a phantom NUL counted) */
  "host::testhost"              /* host slot */
  "\x13"                        /* length 19 (a phantom NUL counted) */
  "sourcetype::syslog"          /* sourcetype slot */
  "\x02"                        /* length 2 (a phantom NUL counted) */
  "1"                           /* cd slot */
  "\x00";                       /* 0 columns */

static const guint8 vector_event_full_header[] =
  "\xfc"                   /* EVENT */
  "\x01"                   /* channel 1 */
  "\xff\x04"               /* flags 0x27f */
  "\x00"                   /* stream id */
  "\x00"                   /* offset */
  "\x00"                   /* suboffset */
  "\x00"                   /* firstid 0 */
  "\x80\xc3\xbb\xc2\x06"   /* timestamp 1750000000 */
  "\x02"                   /* 2 fields */
  "\x04"                   /* type descriptor 0x4 */
  "\x0f"                   /* name length 15 */
  "_MetaData:Index"
  "\x04"                   /* value length 4 */
  "main"
  "\x00"                   /* type descriptor 0x0 */
  "\x05"                   /* name length 5 */
  "count"
  "\x2a"                   /* number 42 */
  "\x42"                   /* payload length 66 */
  "Jul  2 15:04:05 testhost app[123]: árvíztűrő tükörfúrógép";

static const guint8 vector_event_short_header[] =
  "\xfc"       /* EVENT */
  "\x03"       /* channel 3 */
  "\xb5\x04"   /* flags 0x235 */
  "\x00"       /* firstid 0 */
  "\x01"       /* 1 field */
  "\x04"       /* type descriptor 0x4 */
  "\x0f"       /* name length 15 */
  "_MetaData:Index"
  "\x04"       /* value length 4 */
  "main"
  "\x00";      /* payload length 0 */

static const guint8 vector_close_channel[] =
  "\xfd"   /* CLOSE_CHANNEL */
  "\x07";  /* channel 7 */

#endif

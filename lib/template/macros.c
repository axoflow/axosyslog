/*
 * Copyright (c) 2002-2014 Balabit
 * Copyright (c) 1998-2014 Balázs Scheidler
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "template/macros.h"
#include "template/globals.h"
#include "template/escaping.h"
#include "timeutils/cache.h"
#include "timeutils/names.h"
#include "timeutils/unixtime.h"
#include "timeutils/format.h"
#include "timeutils/misc.h"
#include "timeutils/conv.h"
#include "messages.h"
#include "str-format.h"
#include "run-id.h"
#include "host-id.h"
#include "rcptid.h"
#include "logmsg/logmsg.h"
#include "syslog-names.h"
#include "hostname.h"
#include "template/templates.h"
#include "cfg.h"

#include <string.h>

LogMacroDef macros[] =
{
  { "FACILITY", M_FACILITY },
  { "FACILITY_NUM", M_FACILITY_NUM },
  { "SEVERITY", M_SEVERITY },
  { "SEVERITY_NUM", M_SEVERITY_NUM },

  /* these are obsolete aliases of $SEVERITY that we support for compatibility only */
  { "PRIORITY", M_SEVERITY },         /* deprecated */
  { "LEVEL", M_SEVERITY },            /* deprecated */
  { "LEVEL_NUM", M_SEVERITY_NUM },    /* deprecated */

  { "TAG", M_TAG },
  { "TAGS", M_TAGS },
  { "BSDTAG", M_BSDTAG },
  { "PRI", M_PRI },

  { "DATE",           M_DATE },
  { "FULLDATE",       M_FULLDATE },
  { "ISODATE",        M_ISODATE },
  { "STAMP",          M_STAMP },
  { "YEAR",           M_YEAR },
  { "YEAR_DAY",       M_YEAR_DAY },
  { "MONTH",          M_MONTH },
  { "MONTH_WEEK",     M_MONTH_WEEK },
  { "MONTH_ABBREV",   M_MONTH_ABBREV },
  { "MONTH_NAME",     M_MONTH_NAME },
  { "DAY",            M_DAY },
  { "HOUR",           M_HOUR },
  { "HOUR12",         M_HOUR12 },
  { "MIN",            M_MIN },
  { "SEC",            M_SEC },
  { "USEC",           M_USEC },
  { "MSEC",           M_MSEC },
  { "AMPM",           M_AMPM },
  { "WEEKDAY",        M_WEEK_DAY_ABBREV }, /* deprecated */
  { "WEEK_DAY",       M_WEEK_DAY },
  { "WEEK_DAY_ABBREV", M_WEEK_DAY_ABBREV },
  { "WEEK_DAY_NAME",  M_WEEK_DAY_NAME },
  { "WEEK",           M_WEEK },
  { "ISOWEEK",        M_ISOWEEK },
  { "TZOFFSET",       M_TZOFFSET },
  { "TZ",             M_TZ },
  { "SYSUPTIME",      M_SYSUPTIME },
  { "UNIXTIME",       M_UNIXTIME },

  { "R_DATE",           M_RECVD_OFS + M_DATE },
  { "R_FULLDATE",       M_RECVD_OFS + M_FULLDATE },
  { "R_ISODATE",        M_RECVD_OFS + M_ISODATE },
  { "R_STAMP",          M_RECVD_OFS + M_STAMP },
  { "R_YEAR",           M_RECVD_OFS + M_YEAR },
  { "R_YEAR_DAY",       M_RECVD_OFS + M_YEAR_DAY },
  { "R_MONTH",          M_RECVD_OFS + M_MONTH },
  { "R_MONTH_WEEK",     M_RECVD_OFS + M_MONTH_WEEK },
  { "R_MONTH_ABBREV",   M_RECVD_OFS + M_MONTH_ABBREV },
  { "R_MONTH_NAME",     M_RECVD_OFS + M_MONTH_NAME },
  { "R_DAY",            M_RECVD_OFS + M_DAY },
  { "R_HOUR",           M_RECVD_OFS + M_HOUR },
  { "R_HOUR12",         M_RECVD_OFS + M_HOUR12 },
  { "R_MIN",            M_RECVD_OFS + M_MIN },
  { "R_SEC",            M_RECVD_OFS + M_SEC },
  { "R_MSEC",           M_RECVD_OFS + M_MSEC },
  { "R_USEC",           M_RECVD_OFS + M_USEC },
  { "R_AMPM",           M_RECVD_OFS + M_AMPM },
  { "R_WEEKDAY",        M_RECVD_OFS + M_WEEK_DAY_ABBREV }, /* deprecated */
  { "R_WEEK_DAY",       M_RECVD_OFS + M_WEEK_DAY },
  { "R_WEEK_DAY_ABBREV", M_RECVD_OFS + M_WEEK_DAY_ABBREV },
  { "R_WEEK_DAY_NAME",  M_RECVD_OFS + M_WEEK_DAY_NAME },
  { "R_WEEK",           M_RECVD_OFS + M_WEEK },
  { "R_ISOWEEK",        M_RECVD_OFS + M_ISOWEEK },
  { "R_TZOFFSET",       M_RECVD_OFS + M_TZOFFSET },
  { "R_TZ",             M_RECVD_OFS + M_TZ },
  { "R_UNIXTIME",       M_RECVD_OFS + M_UNIXTIME },

  { "S_DATE",           M_STAMP_OFS + M_DATE },
  { "S_FULLDATE",       M_STAMP_OFS + M_FULLDATE },
  { "S_ISODATE",        M_STAMP_OFS + M_ISODATE },
  { "S_STAMP",          M_STAMP_OFS + M_STAMP },
  { "S_YEAR",           M_STAMP_OFS + M_YEAR },
  { "S_YEAR_DAY",       M_STAMP_OFS + M_YEAR_DAY },
  { "S_MONTH",          M_STAMP_OFS + M_MONTH },
  { "S_MONTH_WEEK",     M_STAMP_OFS + M_MONTH_WEEK },
  { "S_MONTH_ABBREV",   M_STAMP_OFS + M_MONTH_ABBREV },
  { "S_MONTH_NAME",     M_STAMP_OFS + M_MONTH_NAME },
  { "S_DAY",            M_STAMP_OFS + M_DAY },
  { "S_HOUR",           M_STAMP_OFS + M_HOUR },
  { "S_HOUR12",         M_STAMP_OFS + M_HOUR12 },
  { "S_MIN",            M_STAMP_OFS + M_MIN },
  { "S_SEC",            M_STAMP_OFS + M_SEC },
  { "S_MSEC",           M_STAMP_OFS + M_MSEC },
  { "S_USEC",           M_STAMP_OFS + M_USEC },
  { "S_AMPM",           M_STAMP_OFS + M_AMPM },
  { "S_WEEKDAY",        M_STAMP_OFS + M_WEEK_DAY_ABBREV }, /* deprecated */
  { "S_WEEK_DAY",       M_STAMP_OFS + M_WEEK_DAY },
  { "S_WEEK_DAY_ABBREV", M_STAMP_OFS + M_WEEK_DAY_ABBREV },
  { "S_WEEK_DAY_NAME",  M_STAMP_OFS + M_WEEK_DAY_NAME },
  { "S_WEEK",           M_STAMP_OFS + M_WEEK },
  { "S_ISOWEEK",        M_STAMP_OFS + M_ISOWEEK },
  { "S_TZOFFSET",       M_STAMP_OFS + M_TZOFFSET },
  { "S_TZ",             M_STAMP_OFS + M_TZ },
  { "S_UNIXTIME",       M_STAMP_OFS + M_UNIXTIME },

  { "C_DATE",           M_CSTAMP_OFS + M_DATE },
  { "C_FULLDATE",       M_CSTAMP_OFS + M_FULLDATE },
  { "C_ISODATE",        M_CSTAMP_OFS + M_ISODATE },
  { "C_STAMP",          M_CSTAMP_OFS + M_STAMP },
  { "C_YEAR",           M_CSTAMP_OFS + M_YEAR },
  { "C_YEAR_DAY",       M_CSTAMP_OFS + M_YEAR_DAY },
  { "C_MONTH",          M_CSTAMP_OFS + M_MONTH },
  { "C_MONTH_WEEK",     M_CSTAMP_OFS + M_MONTH_WEEK },
  { "C_MONTH_ABBREV",   M_CSTAMP_OFS + M_MONTH_ABBREV },
  { "C_MONTH_NAME",     M_CSTAMP_OFS + M_MONTH_NAME },
  { "C_DAY",            M_CSTAMP_OFS + M_DAY },
  { "C_HOUR",           M_CSTAMP_OFS + M_HOUR },
  { "C_HOUR12",         M_CSTAMP_OFS + M_HOUR12 },
  { "C_MIN",            M_CSTAMP_OFS + M_MIN },
  { "C_SEC",            M_CSTAMP_OFS + M_SEC },
  { "C_MSEC",           M_CSTAMP_OFS + M_MSEC },
  { "C_USEC",           M_CSTAMP_OFS + M_USEC },
  { "C_AMPM",           M_CSTAMP_OFS + M_AMPM },
  { "C_WEEKDAY",        M_CSTAMP_OFS + M_WEEK_DAY_ABBREV }, /* deprecated */
  { "C_WEEK_DAY",       M_CSTAMP_OFS + M_WEEK_DAY },
  { "C_WEEK_DAY_ABBREV", M_CSTAMP_OFS + M_WEEK_DAY_ABBREV },
  { "C_WEEK_DAY_NAME",  M_CSTAMP_OFS + M_WEEK_DAY_NAME },
  { "C_WEEK",           M_CSTAMP_OFS + M_WEEK },
  { "C_ISOWEEK",        M_CSTAMP_OFS + M_ISOWEEK },
  { "C_TZOFFSET",       M_CSTAMP_OFS + M_TZOFFSET },
  { "C_TZ",             M_CSTAMP_OFS + M_TZ },
  { "C_UNIXTIME",       M_CSTAMP_OFS + M_UNIXTIME },

  { "P_DATE",           M_PROCESSED_OFS + M_DATE },
  { "P_FULLDATE",       M_PROCESSED_OFS + M_FULLDATE },
  { "P_ISODATE",        M_PROCESSED_OFS + M_ISODATE },
  { "P_STAMP",          M_PROCESSED_OFS + M_STAMP },
  { "P_YEAR",           M_PROCESSED_OFS + M_YEAR },
  { "P_YEAR_DAY",       M_PROCESSED_OFS + M_YEAR_DAY },
  { "P_MONTH",          M_PROCESSED_OFS + M_MONTH },
  { "P_MONTH_WEEK",     M_PROCESSED_OFS + M_MONTH_WEEK },
  { "P_MONTH_ABBREV",   M_PROCESSED_OFS + M_MONTH_ABBREV },
  { "P_MONTH_NAME",     M_PROCESSED_OFS + M_MONTH_NAME },
  { "P_DAY",            M_PROCESSED_OFS + M_DAY },
  { "P_HOUR",           M_PROCESSED_OFS + M_HOUR },
  { "P_HOUR12",         M_PROCESSED_OFS + M_HOUR12 },
  { "P_MIN",            M_PROCESSED_OFS + M_MIN },
  { "P_SEC",            M_PROCESSED_OFS + M_SEC },
  { "P_MSEC",           M_PROCESSED_OFS + M_MSEC },
  { "P_USEC",           M_PROCESSED_OFS + M_USEC },
  { "P_AMPM",           M_PROCESSED_OFS + M_AMPM },
  { "P_WEEKDAY",        M_PROCESSED_OFS + M_WEEK_DAY_ABBREV }, /* deprecated */
  { "P_WEEK_DAY",       M_PROCESSED_OFS + M_WEEK_DAY },
  { "P_WEEK_DAY_ABBREV", M_PROCESSED_OFS + M_WEEK_DAY_ABBREV },
  { "P_WEEK_DAY_NAME",  M_PROCESSED_OFS + M_WEEK_DAY_NAME },
  { "P_WEEK",           M_PROCESSED_OFS + M_WEEK },
  { "P_ISOWEEK",        M_PROCESSED_OFS + M_ISOWEEK },
  { "P_TZOFFSET",       M_PROCESSED_OFS + M_TZOFFSET },
  { "P_TZ",             M_PROCESSED_OFS + M_TZ },
  { "P_UNIXTIME",       M_PROCESSED_OFS + M_UNIXTIME },

  { "SDATA", M_SDATA },
  { "MSGHDR", M_MSGHDR },
  { "SOURCEIP", M_SOURCE_IP },
  { "SOURCEPORT", M_SOURCE_PORT },
  { "DESTIP", M_DEST_IP },
  { "DESTPORT", M_DEST_PORT },
  { "PEERIP", M_PEER_IP },
  { "PEERPORT", M_PEER_PORT },
  { "IP_PROTO", M_IP_PROTOCOL },
  { "PROTO", M_PROTOCOL },
  { "PROTO_NAME", M_PROTOCOL_NAME },
  { "RAWMSG_SIZE", M_RAWMSG_SIZE },
  { "SEQNUM", M_SEQNUM },
  { "CONTEXT_ID", M_CONTEXT_ID },
  { "_", M_CONTEXT_ID },
  { "RCPTID", M_RCPTID },
  { "RUNID", M_RUNID },
  { "HOSTID", M_HOSTID },
  { "UNIQID", M_UNIQID },

  /* values that have specific behaviour with older syslog-ng config versions */
  { "HOST", M_HOST },

  /* message independent macros */
  { "LOGHOST", M_LOGHOST },
  { NULL, 0 }
};


static struct timespec app_uptime;
static GHashTable *macro_hash;

static void
_result_append_value(GString *result, const LogMessage *lm, NVHandle handle, LogMessageValueType *type)
{
  const gchar *str;
  gssize len = 0;

  str = log_msg_get_value_with_type(lm, handle, &len, type);
  g_string_append_len(result, str, len);
}

static gboolean
_is_message_source_an_ip_address(const LogMessage *msg)
{
  if (!msg->saddr)
    return FALSE;
  if (g_sockaddr_inet_check(msg->saddr))
    return TRUE;
#if SYSLOG_NG_ENABLE_IPV6
  if (g_sockaddr_inet6_check(msg->saddr))
    return TRUE;
#endif
  return FALSE;
}

static gboolean
_is_message_dest_an_ip_address(const LogMessage *msg)
{
  if (!msg->daddr)
    return FALSE;
  if (g_sockaddr_inet_check(msg->daddr))
    return TRUE;
#if SYSLOG_NG_ENABLE_IPV6
  if (g_sockaddr_inet6_check(msg->daddr))
    return TRUE;
#endif
  return FALSE;
}

static gint
_get_originating_ip_protocol(const LogMessage *msg)
{
  if (!msg->saddr)
    return 0;
  if (g_sockaddr_inet_check(msg->saddr))
    return 4;
#if SYSLOG_NG_ENABLE_IPV6
  if (g_sockaddr_inet6_check(msg->saddr))
    {
      if (g_sockaddr_inet6_is_v4_mapped(msg->saddr))
        return 4;
      return 6;
    }
#endif
  return 0;
}

const gchar *
_get_protocol_name(gint proto)
{
  switch (proto)
    {
      case IPPROTO_TCP:
        return "tcp";
      case IPPROTO_UDP:
        return "udp";
      default:
        return "unknown";
    }
}

static void
log_macro_expand_date_time(gint id,
                           LogTemplateEvalOptions *options, const LogMessage *msg,
                           GString *result, LogMessageValueType *type)
{
  /* year, month, day */
  const UnixTime *stamp;
  UnixTime sstamp;
  guint tmp_hour;

  if (id >= M_TIME_FIRST && id <= M_TIME_LAST)
    {
      stamp = &msg->timestamps[LM_TS_STAMP];
    }
  else if (id >= M_TIME_FIRST + M_RECVD_OFS && id <= M_TIME_LAST + M_RECVD_OFS)
    {
      id -= M_RECVD_OFS;
      stamp = &msg->timestamps[LM_TS_RECVD];
    }
  else if (id >= M_TIME_FIRST + M_STAMP_OFS && id <= M_TIME_LAST + M_STAMP_OFS)
    {
      id -= M_STAMP_OFS;
      stamp = &msg->timestamps[LM_TS_STAMP];
    }
  else if (id >= M_TIME_FIRST + M_CSTAMP_OFS && id <= M_TIME_LAST + M_CSTAMP_OFS)
    {
      id -= M_CSTAMP_OFS;
      unix_time_set_now(&sstamp);
      stamp = &sstamp;
    }
  else if (id >= M_TIME_FIRST + M_PROCESSED_OFS && id <= M_TIME_LAST + M_PROCESSED_OFS)
    {
      id -= M_PROCESSED_OFS;
      stamp = &msg->timestamps[LM_TS_PROCESSED];

      if (!unix_time_is_set(stamp))
        {
          unix_time_set_now(&sstamp);
          stamp = &sstamp;
        }
    }
  else
    {
      g_assert_not_reached();
      return;
    }

  /* try to use the following zone values in order:
   *   destination specific timezone, if one is specified
   *   message specific timezone, if one is specified
   *   local timezone
   */
  WallClockTime wct;

  convert_unix_time_to_wall_clock_time_with_tz_override(stamp, &wct,
                                                        time_zone_info_get_offset(options->opts->time_zone_info[options->tz], stamp->ut_sec));
  switch (id)
    {
    case M_WEEK_DAY_ABBREV:
      g_string_append_len(result, weekday_names_abbrev[wct.wct_wday], WEEKDAY_NAME_ABBREV_LEN);
      break;
    case M_WEEK_DAY_NAME:
      g_string_append(result, weekday_names[wct.wct_wday]);
      break;
    case M_WEEK_DAY:
      format_uint32_padded(result, 0, 0, 10, wct.wct_wday + 1);
      break;
    case M_WEEK:
      format_uint32_padded(result, 2, '0', 10, (wct.wct_yday - (wct.wct_wday - 1 + 7) % 7 + 7) / 7);
      break;
    case M_ISOWEEK:
      format_uint32_padded(result, 2, '0', 10, wall_clock_time_iso_week_number(&wct));
      break;
    case M_YEAR:
      format_uint32_padded(result, 4, '0', 10, wct.wct_year + 1900);
      break;
    case M_YEAR_DAY:
      format_uint32_padded(result, 3, '0', 10, wct.wct_yday + 1);
      break;
    case M_MONTH:
      format_uint32_padded(result, 2, '0', 10, wct.wct_mon + 1);
      break;
    case M_MONTH_WEEK:
      format_uint32_padded(result, 0, 0, 10, ((wct.wct_mday / 7) +
                                              ((wct.wct_wday > 0) &&
                                               ((wct.wct_mday % 7) >= wct.wct_wday))));
      break;
    case M_MONTH_ABBREV:
      g_string_append_len(result, month_names_abbrev[wct.wct_mon], MONTH_NAME_ABBREV_LEN);
      break;
    case M_MONTH_NAME:
      g_string_append(result, month_names[wct.wct_mon]);
      break;
    case M_DAY:
      format_uint32_padded(result, 2, '0', 10, wct.wct_mday);
      break;
    case M_HOUR:
      format_uint32_padded(result, 2, '0', 10, wct.wct_hour);
      break;
    case M_HOUR12:
      if (wct.wct_hour < 12)
        tmp_hour = wct.wct_hour;
      else
        tmp_hour = wct.wct_hour - 12;

      if (tmp_hour == 0)
        tmp_hour = 12;
      format_uint32_padded(result, 2, '0', 10, tmp_hour);
      break;
    case M_MIN:
      format_uint32_padded(result, 2, '0', 10, wct.wct_min);
      break;
    case M_SEC:
      format_uint32_padded(result, 2, '0', 10, wct.wct_sec);
      break;
    case M_MSEC:
      format_uint32_padded(result, 3, '0', 10, stamp->ut_usec/1000);
      break;
    case M_USEC:
      format_uint32_padded(result, 6, '0', 10, stamp->ut_usec);
      break;
    case M_AMPM:
      g_string_append(result, wct.wct_hour < 12 ? "AM" : "PM");
      break;
    case M_DATE:
      append_format_wall_clock_time(&wct, result, TS_FMT_BSD, options->opts->frac_digits);
      break;
    case M_STAMP:
      if (options->opts->ts_format == TS_FMT_UNIX)
        append_format_unix_time(stamp, result, TS_FMT_UNIX, wct.wct_gmtoff, options->opts->frac_digits);
      else
        append_format_wall_clock_time(&wct, result, options->opts->ts_format, options->opts->frac_digits);
      break;
    case M_ISODATE:
      append_format_wall_clock_time(&wct, result, TS_FMT_ISO, options->opts->frac_digits);
      break;
    case M_FULLDATE:
      append_format_wall_clock_time(&wct, result, TS_FMT_FULL, options->opts->frac_digits);
      break;
    case M_UNIXTIME:
      *type = LM_VT_DATETIME;
      append_format_unix_time(stamp, result, TS_FMT_UNIX, wct.wct_gmtoff, options->opts->frac_digits);
      break;
    case M_TZ:
    case M_TZOFFSET:
      append_format_zone_info(result, wct.wct_gmtoff);
      break;
    default:
      g_assert_not_reached();
      break;
    }
}

gboolean
log_macro_expand(gint id, LogTemplateEvalOptions *options, const LogMessage *msg,
                 GString *result, LogMessageValueType *type)
{
  LogMessageValueType t = LM_VT_STRING;

  switch (id)
    {
    case M_FACILITY:
    {
      /* facility */
      const char *n;

      n = syslog_name_lookup_facility_by_value(msg->pri & SYSLOG_FACMASK);
      if (n)
        {
          g_string_append(result, n);
        }
      else
        {
          format_uint32_padded(result, 0, 0, 16, (msg->pri & SYSLOG_FACMASK) >> 3);
        }
      break;
    }
    case M_FACILITY_NUM:
    {
      t = LM_VT_INTEGER;
      format_uint32_padded(result, 0, 0, 10, (msg->pri & SYSLOG_FACMASK) >> 3);
      break;
    }
    case M_SEVERITY:
    {
      /* level */
      const char *n;

      n = syslog_name_lookup_severity_by_value(msg->pri & SYSLOG_PRIMASK);
      if (n)
        {
          g_string_append(result, n);
        }
      else
        {
          format_uint32_padded(result, 0, 0, 10, msg->pri & SYSLOG_PRIMASK);
        }

      break;
    }
    case M_SEVERITY_NUM:
    {
      t = LM_VT_INTEGER;
      format_uint32_padded(result, 0, 0, 10, msg->pri & SYSLOG_PRIMASK);
      break;
    }
    case M_TAG:
    {
      format_uint32_padded(result, 2, '0', 16, msg->pri);
      break;
    }
    case M_TAGS:
    {
      t = LM_VT_LIST;
      log_msg_format_tags(msg, result, TRUE);
      break;
    }
    case M__ASTERISK:
    {
      t = LM_VT_LIST;
      log_msg_format_matches(msg, result);
      break;
    }
    case M_BSDTAG:
    {
      format_uint32_padded(result, 0, 0, 10, (msg->pri & SYSLOG_PRIMASK));
      g_string_append_c(result, (((msg->pri & SYSLOG_FACMASK) >> 3) + 'A'));
      break;
    }
    case M_PRI:
    {
      format_uint32_padded(result, 0, 0, 10, msg->pri);
      break;
    }
    case M_HOST:
    {
      if (msg->flags & LF_CHAINED_HOSTNAME)
        {
          /* host */
          const gchar *p1, *p2;
          int remaining, length;
          gssize host_len;
          const gchar *host = log_msg_get_value_with_type(msg, LM_V_HOST, &host_len, &t);

          p1 = memchr(host, '@', host_len);

          if (p1)
            p1++;
          else
            p1 = host;
          remaining = host_len - (p1 - host);
          p2 = memchr(p1, '/', remaining);
          length = p2 ? p2 - p1
                   : host_len - (p1 - host);

          g_string_append_len(result, p1, length);
        }
      else
        {
          _result_append_value(result, msg, LM_V_HOST, &t);
        }
      break;
    }
    case M_SDATA:
    {
      log_msg_append_format_sdata(msg, result, options->seq_num);
      break;
    }
    case M_MSGHDR:
    {
      gssize len;
      const gchar *p;

      p = log_msg_get_value_with_type(msg, LM_V_LEGACY_MSGHDR, &len, &t);
      if (len > 0)
        g_string_append_len(result, p, len);
      else
        {
          /* message, complete with program name and pid */
          len = result->len;
          _result_append_value(result, msg, LM_V_PROGRAM, &t);
          if (len != result->len)
            {
              const gchar *pid = log_msg_get_value(msg, LM_V_PID, &len);
              if (len > 0)
                {
                  g_string_append_c(result, '[');
                  g_string_append_len(result, pid, len);
                  g_string_append_c(result, ']');
                }
              g_string_append_len(result, ": ", 2);
            }
        }
      break;
    }
    case M_PEER_IP:
    {
      if (log_msg_is_value_set(msg, LM_V_PEER_IP))
        {
          gssize len;
          const gchar *ip = log_msg_get_value(msg, LM_V_PEER_IP, &len);
          g_string_append_len(result, ip, len);
          break;
        }
      /* fallthrough */
    }
    case M_SOURCE_IP:
    {
      gchar *ip;
      gchar buf[MAX_SOCKADDR_STRING];

      if (_is_message_source_an_ip_address(msg))
        {
          g_sockaddr_format(msg->saddr, buf, sizeof(buf), GSA_ADDRESS_ONLY);
          ip = buf;
        }
      else
        {
          ip = "127.0.0.1";
        }
      g_string_append(result, ip);
      break;
    }
    case M_DEST_IP:
    {
      gchar *ip;
      gchar buf[MAX_SOCKADDR_STRING];

      if (_is_message_dest_an_ip_address(msg))
        {
          g_sockaddr_format(msg->daddr, buf, sizeof(buf), GSA_ADDRESS_ONLY);
          ip = buf;
        }
      else
        {
          ip = "127.0.0.1";
        }
      g_string_append(result, ip);
      break;
    }
    case M_DEST_PORT:
    {
      gint port;

      if (_is_message_dest_an_ip_address(msg))
        {
          port = g_sockaddr_get_port(msg->daddr);
        }
      else
        {
          port = 0;
        }
      t = LM_VT_INTEGER;
      format_uint32_padded(result, 0, 0, 10, port);
      break;
    }
    case M_PEER_PORT:
    {
      if (log_msg_is_value_set(msg, LM_V_PEER_PORT))
        {
          gssize len;
          const gchar *port = log_msg_get_value(msg, LM_V_PEER_PORT, &len);
          t = LM_VT_INTEGER;
          g_string_append_len(result, port, len);
          break;
        }
      /* fallthrough */
    }
    case M_SOURCE_PORT:
    {
      gint port;

      if (_is_message_dest_an_ip_address(msg))
        {
          port = g_sockaddr_get_port(msg->saddr);
        }
      else
        {
          port = 0;
        }
      t = LM_VT_INTEGER;
      format_uint32_padded(result, 0, 0, 10, port);
      break;
    }
    case M_IP_PROTOCOL:
    {
      t = LM_VT_INTEGER;
      format_uint32_padded(result, 0, 0, 10, _get_originating_ip_protocol(msg));
      break;
    }
    case M_PROTOCOL:
    {
      t = LM_VT_INTEGER;
      format_uint32_padded(result, 0, 0, 10, msg->proto);
      break;
    }
    case M_PROTOCOL_NAME:
    {
      g_string_append(result, _get_protocol_name(msg->proto));
      break;
    }
    case M_RAWMSG_SIZE:
    {
      t = LM_VT_INTEGER;
      format_uint32_padded(result, 0, 0, 10, msg->recvd_rawmsg_size);
      break;
    }
    case M_SEQNUM:
    {
      if (options->seq_num)
        {
          format_uint32_padded(result, 0, 0, 10, options->seq_num);
        }
      break;
    }
    case M_CONTEXT_ID:
    {
      if (options->context_id)
        {
          g_string_append(result, options->context_id);
          t = options->context_id_type;
        }
      break;
    }

    case M_RCPTID:
    {
      rcptid_append_formatted_id(result, msg->rcptid);
      break;
    }

    case M_RUNID:
    {
      run_id_append_formatted_id(result);
      break;
    }

    case M_HOSTID:
    {
      host_id_append_formatted_id(result, msg->host_id);
      break;
    }

    case M_UNIQID:
    {
      if (msg->rcptid)
        {
          host_id_append_formatted_id(result, msg->host_id);
          g_string_append(result, "@");
          format_uint64_padded(result, 16, '0', 16, msg->rcptid);
          break;
        }
      break;
    }

    case M_LOGHOST:
    {
      const gchar *hname = options->opts->use_fqdn
                           ? get_local_hostname_fqdn()
                           : get_local_hostname_short();

      g_string_append(result, hname);
      break;
    }
    case M_SYSUPTIME:
    {
      struct timespec ct;

      clock_gettime(CLOCK_MONOTONIC, &ct);
      format_uint64_padded(result, 0, 0, 10, timespec_diff_msec(&ct, &app_uptime) / 10);
      break;
    }

    default:
    {
      log_macro_expand_date_time(id, options, msg, result, &t);
      break;
    }

    }
  if (type)
    *type = t;

  return TRUE;
}

gboolean
log_macro_expand_simple(gint id, const LogMessage *msg, GString *result, LogMessageValueType *type)
{
  LogTemplateEvalOptions options = {log_template_get_global_template_options(), LTZ_LOCAL, 0, NULL, LM_VT_STRING};
  return log_macro_expand(id, &options, msg, result, type);
}

guint
log_macro_lookup(const gchar *macro, gint len)
{
  gchar buf[256];
  gint macro_id;

  g_assert(macro_hash);
  g_strlcpy(buf, macro, MIN(sizeof(buf), len+1));
  gpointer hash_key = g_hash_table_lookup(macro_hash, buf);
  macro_id = GPOINTER_TO_INT(hash_key);

  return macro_id;
}

void
log_macros_global_init(void)
{
  gint i;

  /* init the uptime (SYSUPTIME macro) */
  clock_gettime(CLOCK_MONOTONIC, &app_uptime);

  macro_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  for (i = 0; macros[i].name; i++)
    {
      g_hash_table_insert(macro_hash, g_strdup(macros[i].name),
                          GINT_TO_POINTER(macros[i].id));
    }
  return;
}

void
log_macros_global_deinit(void)
{
  g_hash_table_destroy(macro_hash);
  macro_hash = NULL;
}

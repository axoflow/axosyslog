/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "multi-line/timestamp-multi-line.h"
#include "timeutils/scan-timestamp.h"
#include "timeutils/wallclocktime.h"
#include "messages.h"
#include "reloc.h"
#include "str-utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* the longest prefix of a line a strptime() format is tried on */
#define TIMESTAMP_MULTI_LINE_MAX_SCAN_LEN 128

typedef struct _TimestampMultiLine
{
  MultiLineLogic super;
  gchar *open_chars;
  gchar *close_chars;
} TimestampMultiLine;

/* the strptime() formats of the formats file; loaded once, read-only after
 * that, so the worker threads scanning lines need no locking */
static GPtrArray *formats;

typedef gboolean (*TimestampScanner)(const gchar **buf, gint *left, WallClockTime *wct);

/* in the order of how common they are at the beginning of a log line */
static const TimestampScanner builtin_scanners[] =
{
  /* ISO 8601 with or without fraction and zone, BSD, Cisco and Linksys */
  (TimestampScanner) scan_rfc3164_timestamp,
  /* "10-09-2026 15:31:50.456 +0000", "2026/10/09 15:31:50", "09.10.2026 15:31:50" */
  scan_numeric_date_timestamp,
  /* "09/Oct/2026:15:31:50 +0200", "Thu, 09 Oct 2026 15:31:50", "09-Oct-2026 15:31:50.123" */
  scan_day_month_year_timestamp,
  /* "Thu Oct 09 15:31:50.123456 2026" */
  scan_apache_error_timestamp,
  /* "Oct 09, 2026 3:31:50 PM" */
  scan_java_util_logging_timestamp,
};

static gboolean
_scan_with_builtin_scanners(const guchar **line, gint *left)
{
  for (gsize i = 0; i < G_N_ELEMENTS(builtin_scanners); i++)
    {
      /* a scanner may advance the cursor before it fails */
      const gchar *p = (const gchar *) *line;
      gint l = *left;
      WallClockTime wct;

      wall_clock_time_unset(&wct);
      if (builtin_scanners[i](&p, &l, &wct))
        {
          *line = (const guchar *) p;
          *left = l;
          return TRUE;
        }
    }
  return FALSE;
}

static gboolean
_scan_with_formats(const guchar **line, gint *left)
{
  if (!formats || formats->len == 0)
    return FALSE;

  gchar buf[TIMESTAMP_MULTI_LINE_MAX_SCAN_LEN + 1];
  gsize len = MIN((gsize) *left, TIMESTAMP_MULTI_LINE_MAX_SCAN_LEN);

  memcpy(buf, *line, len);
  buf[len] = 0;

  for (guint i = 0; i < formats->len; i++)
    {
      WallClockTime wct;

      wall_clock_time_unset(&wct);
      const gchar *end = wall_clock_time_strptime(&wct, g_ptr_array_index(formats, i), buf);
      if (end)
        {
          gsize consumed = end - buf;

          *line += consumed;
          *left -= consumed;
          return TRUE;
        }
    }
  return FALSE;
}

/* syslog relayed by a forwarder keeps its "<134>" PRI on TCP and loses it on
 * UDP, and RFC 5424 puts its "1 " version before the timestamp either way */
static void
_skip_syslog_header(const guchar **line, gint *left)
{
  const guchar *p = *line;
  gint l = *left;

  if (l > 0 && *p == '<')
    {
      gint digits = 0;

      p++;
      l--;
      while (l > 0 && digits < 3 && ch_isdigit(*p))
        {
          p++;
          l--;
          digits++;
        }
      if (digits == 0 || l < 1 || *p != '>')
        return;
      p++;
      l--;
      *line = p;
      *left = l;
    }

  if (l >= 3 && p[0] == '1' && p[1] == ' ' && ch_isdigit(p[2]))
    {
      *line = p + 2;
      *left = l - 2;
    }
}

gboolean
timestamp_multi_line_scan(MultiLineLogic *s, const guchar *line, gsize line_len)
{
  TimestampMultiLine *self = (TimestampMultiLine *) s;
  const guchar *p = line;
  gint left = MIN(line_len, (gsize) G_MAXINT);
  gchar close_char = 0;

  _skip_syslog_header(&p, &left);

  if (left > 0 && self->open_chars && *p)
    {
      const gchar *open = strchr(self->open_chars, *p);

      if (open)
        {
          close_char = self->close_chars[open - self->open_chars];
          p++;
          left--;
        }
    }

  if (!_scan_with_builtin_scanners(&p, &left) && !_scan_with_formats(&p, &left))
    return FALSE;

  if (close_char)
    return left > 0 && *p == (guchar) close_char;
  return TRUE;
}

static gint
_accumulate_line(MultiLineLogic *s, const guchar *msg, gsize msg_len, const guchar *segment, gsize segment_len)
{
  /* the first line is the start of an event, whatever it looks like */
  if (msg_len == 0)
    return MLL_CONSUME_SEGMENT | MLL_WAITING;

  if (timestamp_multi_line_scan(s, segment, segment_len))
    return MLL_REWIND_SEGMENT | MLL_EXTRACTED;

  return MLL_CONSUME_SEGMENT | MLL_WAITING;
}

static void
_free(MultiLineLogic *s)
{
  TimestampMultiLine *self = (TimestampMultiLine *) s;

  g_free(self->open_chars);
  g_free(self->close_chars);
  multi_line_logic_free_method(s);
}

MultiLineLogic *
timestamp_multi_line_new(const gchar *pairs)
{
  TimestampMultiLine *self = g_new0(TimestampMultiLine, 1);

  multi_line_logic_init_instance(&self->super);
  self->super.accumulate_line = _accumulate_line;
  self->super.free_fn = _free;

  if (!pairs)
    pairs = TIMESTAMP_MULTI_LINE_DEFAULT_PAIRS;

  gsize n_pairs = strlen(pairs) / 2;
  if (n_pairs)
    {
      self->open_chars = g_malloc(n_pairs + 1);
      self->close_chars = g_malloc(n_pairs + 1);
      for (gsize i = 0; i < n_pairs; i++)
        {
          self->open_chars[i] = pairs[2 * i];
          self->close_chars[i] = pairs[2 * i + 1];
        }
      self->open_chars[n_pairs] = 0;
      self->close_chars[n_pairs] = 0;
    }

  return &self->super;
}

/****************************************************************************
 * The formats file
 ****************************************************************************/

void
timestamp_multi_line_load_formats(const gchar *filename)
{
  if (formats)
    g_ptr_array_free(formats, TRUE);
  formats = g_ptr_array_new_with_free_func(g_free);

  FILE *f = fopen(filename, "r");

  if (!f)
    {
      msg_error("timestamp-multi-line: error opening the timestamp-multi-line.formats file, "
                "only the built-in timestamp formats are recognized",
                evt_tag_str("filename", filename),
                evt_tag_error("error"));
      return;
    }

  gchar line[1024];
  while (fgets(line, sizeof(line), f))
    {
      g_strstrip(line);
      if (line[0] == 0 || line[0] == '#')
        continue;

      msg_trace("timestamp-multi-line: format loaded",
                evt_tag_str("format", line));
      g_ptr_array_add(formats, g_strdup(line));
    }
  fclose(f);
}

void
timestamp_multi_line_global_init(void)
{
  const gchar *filename;
  gchar buf[256];

  if (formats)
    return;

  filename = get_installation_path_for("${pkgdatadir}/timestamp-multi-line.formats");
  if (access(filename, F_OK) < 0)
    {
      const gchar *top_srcdir = getenv("top_srcdir");

      /* we might be running the unit tests */
      g_snprintf(buf, sizeof(buf), "%s%slib/multi-line/timestamp-multi-line.formats",
                 top_srcdir ? : "", top_srcdir ? "/" : "");
      filename = buf;
    }
  timestamp_multi_line_load_formats(filename);
}

void
timestamp_multi_line_global_deinit(void)
{
  if (formats)
    {
      g_ptr_array_free(formats, TRUE);
      formats = NULL;
    }
}

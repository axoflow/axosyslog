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

#ifndef TIMESTAMP_MULTI_LINE_H_INCLUDED
#define TIMESTAMP_MULTI_LINE_H_INCLUDED

#include "multi-line/multi-line-logic.h"

/* the characters a timestamp may be enclosed in, as open/close pairs */
#define TIMESTAMP_MULTI_LINE_DEFAULT_PAIRS "[]"

/* A line that starts with a timestamp starts a new event, any other line
 * continues the previous one, the way a Splunk indexer breaks events by
 * default.  Timestamps are recognized by the hand-written scanners of
 * timeutils/scan-timestamp.h first and by the strptime() formats of the
 * timestamp-multi-line.formats file after that, a syslog PRI and the RFC 5424
 * version before the timestamp are skipped.  @pairs lists the characters
 * a timestamp may be enclosed in, as open/close pairs, NULL for the default.
 */
MultiLineLogic *timestamp_multi_line_new(const gchar *pairs);

/* whether @line starts with a timestamp according to @s, for tests */
gboolean timestamp_multi_line_scan(MultiLineLogic *s, const guchar *line, gsize line_len);

/* replace the strptime() formats with those listed in @filename, which
 * global_init() does with the installed timestamp-multi-line.formats; for
 * tests, as the formats are shared by every instance */
void timestamp_multi_line_load_formats(const gchar *filename);

void timestamp_multi_line_global_init(void);
void timestamp_multi_line_global_deinit(void);

#endif

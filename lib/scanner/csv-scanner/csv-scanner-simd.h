/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Szilard Parrag
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

#ifndef CSVSCANNER_SIMD_H_INCLUDED
#define CSVSCANNER_SIMD_H_INCLUDED

#include "syslog-ng.h"
#include <stdint.h>

#ifdef SYSLOG_NG_MANUAL_AVX2

#include <immintrin.h>

#endif /* SYSLOG_NG_MANUAL_AVX2 */

#ifdef SYSLOG_NG_MANUAL_NEON

#include ???

#endif /* SYSLOG_NG_MANUAL_NEON */


/************************************************************************
 * SIMD primitives
 ************************************************************************/

typedef struct _CSVFieldOfs
{
  gint32 start_ofs;
  gint32 end_ofs;
  gboolean quoted;
} CSVFieldOfs;

gboolean csv_simd_parse(const gchar *input, gsize len, GArray *fields_array);

/* Find the first occurrence of either c1, c2, c3, or c4 in a string.
 * Returns a structure with offset and which char was found or -1 if not found. */
typedef struct
{
  gint offset;  /* offset of first match, or -1 if not found */
  gint which;   /* 0 if c1 found first, 1 if c2, 2 if c3, 3 if c4 */
} CSVSimdFindResult;

void csv_simd_find_either(const char *start, gsize max_len, gchar c1, gchar c2, CSVSimdFindResult *self);

void csv_simd_find_either3(const char *start, gsize max_len, gchar c1, gchar c2, gchar c3, CSVSimdFindResult *self);

void csv_simd_find_either4(const char *start, gsize max_len, gchar c1, gchar c2, gchar c3, gchar c4,
                           CSVSimdFindResult *self);

/* Find the first "interesting" character (delimiter or quote) in unquoted text.
 * Returns offset from start, or -1 if none found in scanned region.
 * This is optimized for quickly skipping normal text when scanning CSV fields. */
gint csv_simd_find_delimiter_or_quote(const char *start, gsize max_len, gchar delimiter, gchar quote_char);

/* Find delimiter, quote, or whitespace in a single pass.
 * Returns: which type was found (0=delimiter, 1=quote, 2=space/tab) and offset.
 * Optimizes whitespace-stripping configurations by doing all classification at once. */
typedef struct
{
  gint offset;  /* offset of first match, or -1 if not found */
  gint which;   /* 0=delimiter, 1=quote, 2=whitespace */
} CSVSimdFindTripleResult;

void csv_simd_find_delim_quote_space(const char *start, gsize max_len, gchar delimiter, gchar quote_char,
                                     CSVSimdFindTripleResult *result);

/* Trim leading/trailing whitespace from field offsets using SIMD.
 * Returns new offset after trimming. Uses AVX2 to scan 32 bytes at a time. */
gint32 csv_simd_trim_leading_whitespace(const gchar *input, gint32 start_ofs, gint32 end_ofs);
gint32 csv_simd_trim_trailing_whitespace(const gchar *input, gint32 start_ofs, gint32 end_ofs);
void csv_detect_cpu_features(void);
gboolean csv_is_simd_available(void);

#endif

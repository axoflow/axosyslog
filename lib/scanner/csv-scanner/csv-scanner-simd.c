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

#include "csv-scanner-simd.h"
#include "compat/glib.h"

static gboolean cpu_supports_simd;

gboolean
csv_is_simd_available(void)
{
  return cpu_supports_simd;
}

/* Per-function target attribute lets GCC and clang emit AVX2 code */
#if defined(SYSLOG_NG_MANUAL_AVX2) && !defined(SYSLOG_NG_MANUAL_NEON)

void
csv_detect_cpu_features(void)
{
  cpu_supports_simd = __builtin_cpu_supports("avx2");
}

/* Per-function target attribute lets GCC and clang emit AVX2 code for
 * this TU without requiring -mavx2 on the command line.  Clang does not
 * treat #pragma GCC target the same as GCC, so the attribute form is the
 * portable option. */
#define AVX2_FN __attribute__((target("avx2")))

/* Parser-level chunk: two AVX2 ymm loads (32 B each) → one 64-bit hit
 * bitmap that we iterate with __builtin_ctzll.  Not a SIMD-imposed
 * constant; the 64 falls out of "two movemasks fit in a uint64_t". */
#define SIMD_PARSER_CHUNK_BYTES 64

/* Per-chunk hit bitmaps: bit i is set iff chunk[i] matches the class.
 * `quotes` collapses '"' and '\'' into a single mask; `spaces` does the
 * same for ' ' and '\t'. */
typedef struct
{
  uint64_t commas;
  uint64_t quotes;
  uint64_t spaces;
} CSVSimdClassification;

/* Helper: combine two 32-bit movemask outputs into a 64-bit chunk bitmap. */
AVX2_FN
static inline uint64_t
_combine_mm(__m256i v_lo, __m256i v_hi)
{
  uint64_t m_lo = (uint32_t) _mm256_movemask_epi8(v_lo);
  uint64_t m_hi = (uint32_t) _mm256_movemask_epi8(v_hi);
  return m_lo | (m_hi << 32);
}

/* Classify 64 bytes starting at `chunk`.  The caller must guarantee that
 * at least 64 bytes are readable at that address. */
AVX2_FN
static inline void
csv_simd_classify_min_64(CSVSimdClassification *self, const char *chunk)
{
  const __m256i comma  = _mm256_set1_epi8(',');
  const __m256i quote  = _mm256_set1_epi8('"');
  const __m256i squote = _mm256_set1_epi8('\'');
  const __m256i space  = _mm256_set1_epi8(' ');
  const __m256i tab    = _mm256_set1_epi8('\t');

  __m256i lo = _mm256_loadu_si256((const __m256i *) chunk);
  __m256i hi = _mm256_loadu_si256((const __m256i *) (chunk + SIMD_PARSER_CHUNK_BYTES / 2));

  /* commas: single character */
  self->commas = _combine_mm(_mm256_cmpeq_epi8(lo, comma),
                             _mm256_cmpeq_epi8(hi, comma));

  /* quotes: '"' | '\''  - fuse the OR in vector domain, movemask once per lane */
  __m256i q_lo = _mm256_or_si256(_mm256_cmpeq_epi8(lo, quote),
                                 _mm256_cmpeq_epi8(lo, squote));
  __m256i q_hi = _mm256_or_si256(_mm256_cmpeq_epi8(hi, quote),
                                 _mm256_cmpeq_epi8(hi, squote));
  self->quotes = _combine_mm(q_lo, q_hi);

  /* spaces: ' ' | '\t' - same trick */
  __m256i s_lo = _mm256_or_si256(_mm256_cmpeq_epi8(lo, space),
                                 _mm256_cmpeq_epi8(lo, tab));
  __m256i s_hi = _mm256_or_si256(_mm256_cmpeq_epi8(hi, space),
                                 _mm256_cmpeq_epi8(hi, tab));
  self->spaces = _combine_mm(s_lo, s_hi);
}

AVX2_FN
static inline gboolean
_simd_parse_consume_hits(const gchar *input, gsize input_len, gsize chunk_base,
                         uint64_t interesting,
                         gchar *quote_state, gint32 *field_start,
                         GArray *fields_array)
{
  while (interesting)
    {
      gint bit = __builtin_ctzll(interesting);
      interesting &= interesting - 1;

      gint32 ofs = (gint32)(chunk_base + bit);
      gchar ch = input[ofs];

      gboolean in_q       = (*quote_state != 0);
      gboolean is_close_q = in_q && (ch == *quote_state);
      gboolean is_comma   = !in_q && (ch == ',');
      gboolean is_quote_c = !in_q && (ch == '"' || ch == '\'');
      gboolean is_open_q  = is_quote_c && (ofs == *field_start);

      /* after a closing quote, the next byte must be ',' or end-of-input */
      if (is_close_q)
        {
          gchar next = ((gsize)(ofs + 1) < input_len) ? input[ofs + 1] : '\0';
          if (next != ',' && next != '\0')
            return FALSE; /* bail */
        }

      if (is_close_q || (is_comma && *field_start != -1))
        {
          CSVFieldOfs field =
          {
            .start_ofs = *field_start,
            .end_ofs = ofs,
            .quoted = is_close_q,
          };
          g_array_append_val(fields_array, field);
        }

      if (is_open_q || is_comma)
        *field_start = ofs + 1;
      else if (is_close_q)
        *field_start = -1;

      if (is_open_q)
        *quote_state = ch;
      else if (is_close_q)
        *quote_state = 0;
    }
  return TRUE;
}

AVX2_FN
gboolean
csv_simd_parse(const gchar *input, gsize len, GArray *fields_array)
{
  if (len == 0)
    return TRUE;

  gchar quote_state = 0;
  gint32 field_start = 0;

  /* Phase 1: full 64-byte chunks read directly from input */
  gsize n_full_chunks = len / SIMD_PARSER_CHUNK_BYTES;
  for (gsize i = 0; i < n_full_chunks; i++)
    {
      const gchar *chunk_ptr = input + i * SIMD_PARSER_CHUNK_BYTES;
      CSVSimdClassification m;
      csv_simd_classify_min_64(&m, chunk_ptr);
      uint64_t interesting = m.commas | m.quotes;

      if (!_simd_parse_consume_hits(input, len, i * SIMD_PARSER_CHUNK_BYTES, interesting,
                                    &quote_state, &field_start, fields_array))
        {
          g_array_set_size(fields_array, 0);
          return FALSE;
        }
    }

  /* Phase 2: tail (< 64 bytes) */
  gsize tail_start = n_full_chunks * SIMD_PARSER_CHUNK_BYTES;
  gsize tail_len = len - tail_start;
  if (tail_len)
    {
      gchar tail_buf[SIMD_PARSER_CHUNK_BYTES];
      memcpy(tail_buf, input + tail_start, tail_len);
      memset(tail_buf + tail_len, 0, SIMD_PARSER_CHUNK_BYTES - tail_len);

      CSVSimdClassification m;
      csv_simd_classify_min_64(&m, tail_buf);
      uint64_t interesting = m.commas | m.quotes;

      if (!_simd_parse_consume_hits(input, len, tail_start, interesting,
                                    &quote_state, &field_start, fields_array))
        {
          g_array_set_size(fields_array, 0);
          return FALSE;
        }
    }

  /* End-of-input: emit the trailing unquoted field (if any) and detect
   * an unterminated quoted field. */
  if (quote_state)
    {
      g_array_set_size(fields_array, 0);
      return FALSE;
    }
  if (field_start != -1 && field_start < (gint32) len)
    {
      CSVFieldOfs field =
      {
        .start_ofs = field_start,
        .end_ofs = (gint32) len,
        .quoted = FALSE,
      };
      g_array_append_val(fields_array, field);
    }
  return TRUE;
}

/* ---------------------------------------------------------------------
 * Multi-needle find: scan up to 4 single-byte needles in parallel.
 *
 * Strategy:
 *   - precompute cmpeq vectors per needle for both 32-byte halves
 *   - OR them all together and use _mm256_testz to skip blank chunks
 *     in vector domain (no movemask), then only on a hit pay the
 *     movemask cost to figure out which needle/where.
 * --------------------------------------------------------------------- */

/* Scalar tail search for regions smaller than SIMD_PARSER_CHUNK_BYTES. */
AVX2_FN
static inline void
_scalar_search_impl(const char *start, gsize offset, gsize remaining, const gchar *chars, gsize num_chars,
                    CSVSimdFindResult *result)
{
  for (gsize i = 0; i < remaining; i++)
    {
      gchar ch = start[offset + i];
      for (gsize j = 0; j < num_chars; j++)
        {
          if (ch == chars[j])
            {
              result->offset = offset + i;
              result->which = j;
              return;
            }
        }
    }
  result->offset = -1;
  result->which = 0;
}

/* Compute the (offset, which) of the earliest hit across `n` 64-bit masks.
 * For ties (multiple needles match the same byte), the lowest index wins. */
static inline void
_first_hit(const uint64_t *masks, int n, gsize chunk_offset, CSVSimdFindResult *result)
{
  uint64_t any = 0;
  for (int k = 0; k < n; k++)
    any |= masks[k];

  int pos = __builtin_ctzll(any);
  int which = 0;
  for (int k = 0; k < n; k++)
    if ((masks[k] >> pos) & 1)
      {
        which = k;
        break;
      }

  result->offset = chunk_offset + pos;
  result->which = which;
}

/* Single SIMD inner step: scan 64 bytes for any of `n` needles. */
AVX2_FN
static inline gboolean
_find_in_chunk(const char *start, gsize offset, const __m256i *needles, int n,
               CSVSimdFindResult *result)
{
  __m256i lo = _mm256_loadu_si256((const __m256i *) (start + offset));
  __m256i hi = _mm256_loadu_si256((const __m256i *) (start + offset + SIMD_PARSER_CHUNK_BYTES / 2));

  /* Per-needle cmpeq, kept around so we can produce per-needle bitmaps
   * cheaply if there's a hit.  ORing in vector domain plus testz keeps
   * the hot no-hit path off port 5 (movemask). */
  __m256i cmp_lo[4], cmp_hi[4];
  __m256i any_lo = _mm256_setzero_si256();
  __m256i any_hi = _mm256_setzero_si256();
  for (int k = 0; k < n; k++)
    {
      cmp_lo[k] = _mm256_cmpeq_epi8(lo, needles[k]);
      cmp_hi[k] = _mm256_cmpeq_epi8(hi, needles[k]);
      any_lo = _mm256_or_si256(any_lo, cmp_lo[k]);
      any_hi = _mm256_or_si256(any_hi, cmp_hi[k]);
    }
  __m256i any = _mm256_or_si256(any_lo, any_hi);
  if (_mm256_testz_si256(any, any))
    return FALSE;

  uint64_t masks[4];
  for (int k = 0; k < n; k++)
    masks[k] = _combine_mm(cmp_lo[k], cmp_hi[k]);
  _first_hit(masks, n, offset, result);
  return TRUE;
}

/* Unified n-needle linear scan; n in [1, 4]. */
AVX2_FN
static inline void
_find_n(const char *start, gsize max_len, const gchar *chars, int n,
        CSVSimdFindResult *result)
{
  if (G_LIKELY(cpu_supports_simd))
    {
      __m256i needles[4];
      for (int k = 0; k < n; k++)
        needles[k] = _mm256_set1_epi8(chars[k]);

      gsize offset = 0;
      while (offset + SIMD_PARSER_CHUNK_BYTES <= max_len)
        {
          if (_find_in_chunk(start, offset, needles, n, result))
            return;
          offset += SIMD_PARSER_CHUNK_BYTES;
        }

      if (offset < max_len)
        {
          _scalar_search_impl(start, offset, max_len - offset, chars, n, result);
          return;
        }
    }
  else
    {
      // fallback path on CPUs without AVX2
      _scalar_search_impl(start, 0, max_len, chars, n, result);
      return;
    }

  result->offset = -1;
  result->which = 0;

}

AVX2_FN
void
csv_simd_find_either(const char *start, gsize max_len, gchar c1, gchar c2, CSVSimdFindResult *result)
{
  const gchar chars[2] = { c1, c2 };
  _find_n(start, max_len, chars, 2, result);
}

AVX2_FN
void
csv_simd_find_either3(const char *start, gsize max_len, gchar c1, gchar c2, gchar c3, CSVSimdFindResult *result)
{
  const gchar chars[3] = { c1, c2, c3 };
  _find_n(start, max_len, chars, 3, result);
}

AVX2_FN
void
csv_simd_find_either4(const char *start, gsize max_len, gchar c1, gchar c2, gchar c3, gchar c4,
                      CSVSimdFindResult *result)
{
  const gchar chars[4] = { c1, c2, c3, c4 };
  _find_n(start, max_len, chars, 4, result);
}

AVX2_FN
void
csv_simd_find_delim_quote_space(const char *start, gsize max_len, gchar delimiter, gchar quote_char,
                                CSVSimdFindTripleResult *result)
{
  CSVSimdFindResult sr;
  csv_simd_find_either4(start, max_len, delimiter, quote_char, ' ', '\t', &sr);

  result->offset = sr.offset;
  /* Normalize: which=0 (delim), 1 (quote), 2 (space), 3 (tab) -> 0, 1, 2, 2 */
  result->which = (sr.which == 3) ? 2 : sr.which;
}

AVX2_FN
gint
csv_simd_find_delimiter_or_quote(const char *start, gsize max_len, gchar delimiter, gchar quote_char)
{
  CSVSimdFindResult result;
  csv_simd_find_either(start, max_len, delimiter, quote_char, &result);
  return result.offset;
}

/* Trim leading whitespace using SIMD scan (32 bytes at a time).
 * Returns the new (inclusive) start offset. */
AVX2_FN
gint32
csv_simd_trim_leading_whitespace(const gchar *input, gint32 start_ofs, gint32 end_ofs)
{
  const __m256i spaces = _mm256_set1_epi8(' ');
  const __m256i tabs   = _mm256_set1_epi8('\t');

  /* One ymm load per step; 32 is the AVX2 register width in bytes. */
  while (end_ofs - start_ofs >= 32)
    {
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + start_ofs));
      __m256i is_ws = _mm256_or_si256(_mm256_cmpeq_epi8(v, spaces),
                                      _mm256_cmpeq_epi8(v, tabs));
      uint32_t non_ws = ~(uint32_t) _mm256_movemask_epi8(is_ws);
      if (non_ws)
        return start_ofs + __builtin_ctz(non_ws);
      start_ofs += 32;
    }

  while (start_ofs < end_ofs && (input[start_ofs] == ' ' || input[start_ofs] == '\t'))
    start_ofs++;
  return start_ofs;
}

/* Trim trailing whitespace using SIMD scan (32 bytes at a time).
 * Returns the new (exclusive) end offset.
 *
 * Operates entirely on offsets - never forms `input + start_ofs - 1`,
 * which would be UB when start_ofs == 0. */
AVX2_FN
gint32
csv_simd_trim_trailing_whitespace(const gchar *input, gint32 start_ofs, gint32 end_ofs)
{
  const __m256i spaces = _mm256_set1_epi8(' ');
  const __m256i tabs   = _mm256_set1_epi8('\t');

  /* One ymm load per step; 32 is the AVX2 register width in bytes. */
  while (end_ofs - start_ofs >= 32)
    {
      gint32 chunk_start = end_ofs - 32;
      __m256i v = _mm256_loadu_si256((const __m256i *)(input + chunk_start));
      __m256i is_ws = _mm256_or_si256(_mm256_cmpeq_epi8(v, spaces),
                                      _mm256_cmpeq_epi8(v, tabs));
      uint32_t non_ws = ~(uint32_t) _mm256_movemask_epi8(is_ws);
      if (non_ws)
        return chunk_start + (31 - __builtin_clz(non_ws)) + 1;
      end_ofs = chunk_start;
    }

  while (end_ofs > start_ofs && (input[end_ofs - 1] == ' ' || input[end_ofs - 1] == '\t'))
    end_ofs--;
  return end_ofs;
}

#elif defined(SYSLOG_NG_MANUAL_NEON) && !defined(SYSLOG_NG_MANUAL_AVX2)

#include <sys/auxv.h>
#include <asm/hwcap.h>

void
csv_detect_cpu_features(void)
{
  cpu_supports_simd = getauxval (AT_HWCAP) & HWCAP_NEON;
}

gboolean
csv_simd_parse(const gchar *input, gsize len, GArray *fields_array)
{
  return FALSE;
}

void
csv_simd_find_either(const char *start, gsize max_len, gchar c1, gchar c2, CSVSimdFindResult *self)
{
  self->offset = -1;
  self->which = 0;
  return;
}

void
csv_simd_find_either3(const char *start, gsize max_len, gchar c1, gchar c2, gchar c3, CSVSimdFindResult *self)
{
  self->offset = -1;
  self->which = 0;
  return;
}

void
csv_simd_find_either4(const char *start, gsize max_len, gchar c1, gchar c2, gchar c3, gchar c4,
                           CSVSimdFindResult *self)
{
  self->offset = -1;
  self->which = 0;
  return;
}

gint
csv_simd_find_delimiter_or_quote(const char *start, gsize max_len, gchar delimiter, gchar quote_char)
{
  return -1;
}

void
csv_simd_find_delim_quote_space(const char *start, gsize max_len, gchar delimiter, gchar quote_char,
                                     CSVSimdFindTripleResult *result)
{
  result->offset = -1;
  result->which = 0;
  return;
}

gint32
csv_simd_trim_leading_whitespace(const gchar *input, gint32 start_ofs, gint32 end_ofs)
{
  return -1;
}

gint32
csv_simd_trim_trailing_whitespace(const gchar *input, gint32 start_ofs, gint32 end_ofs)
{
  return -1;
}

#else

gboolean
csv_simd_parse(const gchar *input, gsize len, GArray *fields_array)
{
  return FALSE;
}

static inline void
_scalar_search_impl(const char *start, gsize offset, gsize remaining, const gchar *chars, gsize num_chars,
                    CSVSimdFindResult *result)
{
  for (gsize i = 0; i < remaining; i++)
    {
      gchar ch = start[offset + i];
      for (gsize j = 0; j < num_chars; j++)
        {
          if (ch == chars[j])
            {
              result->offset = offset + i;
              result->which = j;
              return;
            }
        }
    }
  result->offset = -1;
  result->which = 0;
}

void
csv_simd_find_either(const char *start, gsize max_len, gchar c1, gchar c2, CSVSimdFindResult *self)
{
  const gchar chars[2] = { c1, c2 };
  _scalar_search_impl(start, 0, max_len, chars, 2, self);
}

void
csv_simd_find_either3(const char *start, gsize max_len, gchar c1, gchar c2, gchar c3, CSVSimdFindResult *self)
{
  self->offset = -1;
  self->which = 0;
  return;
}

void
csv_simd_find_either4(const char *start, gsize max_len, gchar c1, gchar c2, gchar c3, gchar c4,
                           CSVSimdFindResult *self)
{
  self->offset = -1;
  self->which = 0;
  return;
}

gint
csv_simd_find_delimiter_or_quote(const char *start, gsize max_len, gchar delimiter, gchar quote_char)
{
  return -1;
}

void
csv_simd_find_delim_quote_space(const char *start, gsize max_len, gchar delimiter, gchar quote_char,
                                     CSVSimdFindTripleResult *result)
{
  result->offset = -1;
  result->which = 0;
  return;
}

gint32
csv_simd_trim_leading_whitespace(const gchar *input, gint32 start_ofs, gint32 end_ofs)
{
  return -1;
}

gint32
csv_simd_trim_trailing_whitespace(const gchar *input, gint32 start_ofs, gint32 end_ofs)
{
  return -1;
}

void
csv_detect_cpu_features(void)
{
  cpu_supports_simd = 0;
}

#endif

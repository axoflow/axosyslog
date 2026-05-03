/*
 * Copyright (c) 2002-2015 Balabit
 * Copyright (c) 1998-2015 Balázs Scheidler
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
#include "csv-scanner.h"
#include "csv-scanner-simd.h"
#include "str-utils.h"
#include "string-list.h"
#include "scratch-buffers.h"
#include "messages.h"
#include "compat/string.h"

/************************************************************************
 * CSVScannerOptions
 ************************************************************************/

void
csv_scanner_options_set_flags(CSVScannerOptions *options, guint32 flags)
{
  options->flags = flags;
}

void
csv_scanner_options_set_dialect(CSVScannerOptions *options, CSVScannerDialect dialect)
{
  options->dialect = dialect;
}

void
csv_scanner_options_set_delimiters(CSVScannerOptions *options, const gchar *delimiters)
{
  g_free(options->delimiters);

  if (!delimiters || strcmp(delimiters, ",") == 0)
    options->delimiters = NULL;
  else
    options->delimiters = g_strdup(delimiters);
}

void
csv_scanner_options_set_string_delimiters(CSVScannerOptions *options, GList *string_delimiters)
{
  string_list_free(options->string_delimiters);
  options->string_delimiters = string_delimiters;
}

void
csv_scanner_options_set_quotes_start_and_end(CSVScannerOptions *options, const gchar *quotes_start,
                                             const gchar *quotes_end)
{
  g_free(options->quotes_start);
  g_free(options->quotes_end);
  options->quotes_start = g_strdup(quotes_start);
  options->quotes_end = g_strdup(quotes_end);
}

void
csv_scanner_options_set_quotes(CSVScannerOptions *options, const gchar *quotes)
{
  csv_scanner_options_set_quotes_start_and_end(options, quotes, quotes);
}

void
csv_scanner_options_set_quote_pairs(CSVScannerOptions *options, const gchar *quote_pairs)
{
  gint i;

  g_free(options->quotes_start);
  g_free(options->quotes_end);

  /* this is the fast-path, NULL values will trigger the hard-coded implementation */
  if (!quote_pairs || strcmp(quote_pairs, "\"\"''") == 0)
    {
      options->quotes_start = NULL;
      options->quotes_end = NULL;
      return;
    }

  options->quotes_start = g_malloc((strlen(quote_pairs) / 2) + 1);
  options->quotes_end = g_malloc((strlen(quote_pairs) / 2) + 1);

  for (i = 0; quote_pairs[i] && quote_pairs[i+1]; i += 2)
    {
      options->quotes_start[i / 2] = quote_pairs[i];
      options->quotes_end[i / 2] = quote_pairs[i + 1];
    }
  options->quotes_start[i / 2] = 0;
  options->quotes_end[i / 2] = 0;
}


void
csv_scanner_options_set_null_value(CSVScannerOptions *options, const gchar *null_value)
{
  g_free(options->null_value);
  options->null_value = null_value && null_value[0] ? g_strdup(null_value) : NULL;
}

void
csv_scanner_options_copy(CSVScannerOptions *dst, CSVScannerOptions *src)
{
  csv_scanner_options_set_delimiters(dst, src->delimiters);
  csv_scanner_options_set_quotes_start_and_end(dst, src->quotes_start, src->quotes_end);
  csv_scanner_options_set_null_value(dst, src->null_value);
  csv_scanner_options_set_string_delimiters(dst, string_list_clone(src->string_delimiters));
  dst->dialect = src->dialect;
  dst->flags = src->flags;
}

void
csv_scanner_options_clean(CSVScannerOptions *options)
{
  g_free(options->quotes_start);
  g_free(options->quotes_end);
  g_free(options->null_value);
  g_free(options->delimiters);
  string_list_free(options->string_delimiters);
}

gboolean
csv_scanner_options_validate(CSVScannerOptions *options, gint expected_columns)
{
  if(expected_columns == 0 && (options->flags & CSV_SCANNER_GREEDY))
    {
      msg_error("The greedy flag of csv-parser can not be used without specifying the columns() option");
      return FALSE;
    }

  return TRUE;
}

/************************************************************************
 * CSVScanner
 ************************************************************************/

#define DEFAULT_DELIM_CHAR ','

static gboolean
_is_whitespace_char(gchar ch)
{
  return (ch == ' ' || ch == '\t');
}

static gboolean
_is_delimiter_char(CSVScanner *self, gchar ch)
{
  if (self->options->delimiters)
    return _strchr_optimized_for_single_char_haystack(self->options->delimiters, ch) != NULL;
  return ch == DEFAULT_DELIM_CHAR;
}

static void
_skip_whitespace(CSVScanner *self, const gchar **src)
{
  if (self->current_quote)
    {
      /* quoted value, all whitespace is to be removed, even if the delimiter is considered whitespace */
      while (_is_whitespace_char(**src))
        (*src)++;
    }
  else
    {
      /* in case the value is unquoted, delimiters are never considered
       * whitespace.  This plays a role in case the delimiter is either a
       * space or a tab.  In those cases, a new delimiter starts the next
       * new value to be extracted.  */
      while (_is_whitespace_char(**src))
        {
          if (_is_delimiter_char(self, **src))
            break;
          (*src)++;
        }
    }
}

static void
_parse_opening_quote_character_generic(CSVScanner *self)
{
  const gchar *quote = _strchr_optimized_for_single_char_haystack(self->options->quotes_start, *self->src);

  if (quote != NULL)
    {
      /* ok, quote character found */
      self->src++;
      self->current_quote = self->options->quotes_end[quote - self->options->quotes_start];
    }
  else
    {
      /* we didn't start with a quote character, no need for escaping, delimiter terminates */
      self->current_quote = 0;
    }
}

static void
_parse_opening_quote_character(CSVScanner *self)
{
  if (self->options->quotes_start)
    {
      _parse_opening_quote_character_generic(self);
      return;
    }
  if (*self->src == '"' || *self->src == '\'')
    {
      /* ok, quote character found */
      self->current_quote = *self->src;
      self->src++;
    }
  else
    {
      self->current_quote = 0;
    }
}

static void
_parse_left_whitespace(CSVScanner *self)
{
  if ((self->options->flags & CSV_SCANNER_STRIP_WHITESPACE) == 0)
    return;

  _skip_whitespace(self, &self->src);
}

static gint
_decode_xdigit(gchar xdigit)
{
  if (xdigit >= '0' && xdigit <= '9')
    return xdigit - '0';
  if (xdigit >= 'a' && xdigit <= 'f')
    return xdigit - 'a' + 10;
  if (xdigit >= 'A' && xdigit <= 'F')
    return xdigit - 'A' + 10;
  return -1;
}

static gint
_decode_xbyte(gchar xdigit1, gchar xdigit2)
{
  gint nibble_hi, nibble_lo;

  nibble_hi = _decode_xdigit(xdigit1);
  nibble_lo = _decode_xdigit(xdigit2);
  if (nibble_hi < 0 || nibble_lo < 0)
    return -1;
  return (nibble_hi << 4) + nibble_lo;
}

static void
_parse_characters_with_quotation(CSVScanner *self, gboolean *nonliteral_input)
{
  gchar ch;
  CSVSimdFindResult result;
  gsize remaining = self->input_end - self->src;
  csv_simd_find_either(self->src, remaining, '\\', self->current_quote, &result);
  const gchar *nexthop;

  if (result.offset >= 0)
    nexthop = self->src + result.offset;
  else
    nexthop = self->src + remaining;  /* no match: scan to end of input */

  if (nexthop > self->src)
    {
      gsize len = nexthop - self->src;
      g_string_append_len(self->current_value, self->src, len);
      self->src += len;
      if (*nexthop == 0)
        return;
    }

  /* quoted character */
  if (self->options->dialect == CSV_SCANNER_ESCAPE_DOUBLE_CHAR &&
      *self->src == self->current_quote &&
      *(self->src+1) == self->current_quote)
    {
      self->src++;
      ch = *self->src;
      *nonliteral_input = TRUE;
    }
  else if (self->options->dialect == CSV_SCANNER_ESCAPE_BACKSLASH &&
           *self->src == '\\' &&
           *(self->src + 1))
    {
      self->src++;
      ch = *self->src;
      *nonliteral_input = TRUE;
    }
  else if (self->options->dialect == CSV_SCANNER_ESCAPE_BACKSLASH_WITH_SEQUENCES &&
           *self->src == '\\' &&
           *(self->src + 1))
    {
      self->src++;
      ch = *self->src;
      *nonliteral_input = TRUE;
      if (ch != self->current_quote)
        {
          switch (ch)
            {
            case 'a':
              ch = '\a';
              break;
            case 'n':
              ch = '\n';
              break;
            case 'r':
              ch = '\r';
              break;
            case 't':
              ch = '\t';
              break;
            case 'v':
              ch = '\v';
              break;
            case 'x':
              if (*(self->src+1) && *(self->src+2))
                {
                  gint decoded = _decode_xbyte(*(self->src+1), *(self->src+2));
                  if (decoded >= 0)
                    {
                      self->src += 2;
                      ch = decoded;
                    }
                  else
                    ch = 'x';
                }
              break;
            default:
              break;
            }
        }
    }
  else if (*self->src == self->current_quote)
    {
      self->current_quote = 0;
      goto exit;
    }
  else
    {
      ch = *self->src;
    }
  g_string_append_c(self->current_value, ch);
exit:
  self->src++;
}

/* searches for str in list and returns the first occurrence, otherwise NULL */
static gboolean
_match_string_delimiters_at_current_position(const char *input, GList *string_delimiters, int *result_length)
{
  GList *l;

  for (l = string_delimiters; l; l = l->next)
    {
      gint len = strlen(l->data);

      if (strncmp(input, l->data, len) == 0)
        {
          *result_length = len;
          return TRUE;
        }
    }
  return FALSE;
}

static gboolean
_parse_string_delimiters_at_current_position(CSVScanner *self)
{
  gint delim_len;

  if (!self->options->string_delimiters)
    return FALSE;

  if (_match_string_delimiters_at_current_position(self->src,
                                                   self->options->string_delimiters,
                                                   &delim_len))
    {
      self->src += delim_len;
      return TRUE;
    }
  return FALSE;
}

static gboolean
_parse_character_delimiters_at_current_position(CSVScanner *self, gboolean *nonliteral_input)
{
  gboolean quoted = FALSE;
  if (self->options->dialect == CSV_SCANNER_ESCAPE_UNQUOTED_DELIMITER &&
      *self->src == '\\' &&
      *(self->src + 1))
    {
      self->src++;
      *nonliteral_input = quoted = TRUE;
    }
  if (_is_delimiter_char(self, *self->src))
    {
      if (quoted)
        return FALSE;
      self->src++;
      return TRUE;
    }
  if (quoted)
    self->src--;
  return FALSE;
}

static gboolean
_parse_delimiter(CSVScanner *self, gboolean *nonliteral_input)
{
  if (_parse_string_delimiters_at_current_position(self))
    return TRUE;

  if (_parse_character_delimiters_at_current_position(self, nonliteral_input))
    return TRUE;

  return FALSE;
}

static void
_parse_unquoted_literal_characters_generic(CSVScanner *self)
{
  g_string_append_c(self->current_value, *self->src);
  self->src++;
}

static void
_parse_unquoted_literal_characters(CSVScanner *self, gboolean *nonliteral_input)
{
  if (self->options->string_delimiters || self->options->delimiters)
    {
      _parse_unquoted_literal_characters_generic(self);
      return;
    }

  const gchar *delim = strchrnul(self->src, DEFAULT_DELIM_CHAR);
  const gchar *backslash = NULL;
  while (delim > self->src)
    {
      if (self->options->dialect == CSV_SCANNER_ESCAPE_UNQUOTED_DELIMITER)
        {
          if (!backslash)
            backslash = strchrnul(self->src, '\\');
          if (*backslash && backslash < delim)
            {
              /* backslash before the delimiter, let's copy the value and the escaped character to self->current_value */

              gsize len = backslash - self->src;
              gboolean escaped_delimiter = (backslash + 1) == delim;

              g_string_append_len(self->current_value, self->src, len);
              self->src += len + 1;
              g_string_append_c(self->current_value, *self->src);
              self->src++;

              if (escaped_delimiter)
                delim = strchrnul(self->src, DEFAULT_DELIM_CHAR);
              backslash = NULL;
              *nonliteral_input = TRUE;
              continue;
            }
          else
            {
              /* backslash is outside of the current value */
            }
        }

      /* copy everything up to the delimier */
      gsize len = delim - self->src;

      g_string_append_len(self->current_value, self->src, len);
      self->src += len;
      break;
    }
}

static void
_parse_value_with_whitespace_and_delimiter(CSVScanner *self)
{
  /* was there any escaping in the source string? */
  gboolean nonliteral_input = FALSE;

  self->current_value_start_ofs = self->src - self->input;

  /* SIMD fast path for simple unquoted values with default delimiter */
  if (!self->current_quote && !self->options->string_delimiters && !self->options->delimiters &&
      self->options->dialect == CSV_SCANNER_ESCAPE_NONE && !(self->options->flags & CSV_SCANNER_STRIP_WHITESPACE))
    {
      gsize remaining = self->input_end - self->src;
      CSVSimdFindResult result;
      /* Find delimiter, double-quote, or single-quote in a single SIMD pass */
      csv_simd_find_either3(self->src, remaining, DEFAULT_DELIM_CHAR, '"', '\'', &result);

      if (result.offset > 0)
        {
          /* Found delimiter or quote, copy up to it */
          g_string_append_len(self->current_value, self->src, result.offset);
          self->src += result.offset;
          if (*self->src == DEFAULT_DELIM_CHAR)
            {
              self->src++;
              return;
            }
          /* Fall through to handle quote */
          self->current_quote = *self->src;
          self->src++;
          goto continuation;
        }
      else if (result.offset == 0)
        {
          /* Delimiter at start */
          self->src++;
          return;
        }
    }

continuation:
  while (*self->src)
    {
      if (self->current_quote)
        {
          /* within quotation marks */
          _parse_characters_with_quotation(self, &nonliteral_input);
        }
      else
        {
          /* unquoted value */
          if (_parse_delimiter(self, &nonliteral_input))
            break;
          _parse_unquoted_literal_characters(self, &nonliteral_input);
        }
    }
  if (nonliteral_input)
    self->current_value_start_ofs = -1;
}

static gint
_get_value_length_without_right_whitespace(CSVScanner *self)
{
  gint len = self->current_value->len;

  /* if the value was quoted, we can get rid off whitespace even if they are
   * part of delimiters.  In any other case we won't have delimiters as
   * whitespace on the right side (as they started the next value) */

  while (len > 0 && _is_whitespace_char(*(self->current_value->str + len - 1)))
    len--;

  return len;
}

static inline void
_translate_rstrip_whitespace(CSVScanner *self)
{
  if (self->options->flags & CSV_SCANNER_STRIP_WHITESPACE)
    g_string_truncate(self->current_value, _get_value_length_without_right_whitespace(self));
}

static void
_translate_null_value(CSVScanner *self)
{
  if (self->options->null_value &&
      strcmp(self->current_value->str, self->options->null_value) == 0)
    g_string_truncate(self->current_value, 0);
}

static void
_translate_value(CSVScanner *self)
{
  _translate_rstrip_whitespace(self);
  _translate_null_value(self);
}

static gboolean
_is_last_column(CSVScanner *self)
{
  if (self->expected_columns == 0)
    return FALSE;

  if (self->current_column == self->expected_columns - 1)
    return TRUE;

  return FALSE;
}

static gboolean
_switch_to_next_column(CSVScanner *self)
{
  g_string_truncate(self->current_value, 0);
  self->current_value_start_ofs = -1;

  if (self->expected_columns == 0)
    return TRUE;

  switch (self->state)
    {
    case CSV_STATE_INITIAL:
      self->state = CSV_STATE_COLUMNS;
      self->current_column = 0;
      if (self->expected_columns > 0)
        return TRUE;
      self->state = CSV_STATE_FINISH;
      return FALSE;
    case CSV_STATE_COLUMNS:
    case CSV_STATE_GREEDY_COLUMN:
      self->current_column++;
      if (self->current_column <= self->expected_columns - 1)
        return TRUE;
      self->state = CSV_STATE_FINISH;
      return FALSE;
    case CSV_STATE_PARTIAL_INPUT:
    case CSV_STATE_FINISH:
      return FALSE;
    default:
      break;
    }
  g_assert_not_reached();
}

static gboolean
_take_rest(CSVScanner *self)
{
  _parse_left_whitespace(self);
  self->current_value_start_ofs = self->src - self->input;
  g_string_assign(self->current_value, self->src);
  self->src += self->current_value->len;
  self->state = CSV_STATE_GREEDY_COLUMN;
  _translate_value(self);
  return TRUE;
}

gboolean
csv_scanner_append_rest(CSVScanner *self)
{
  if (self->state == CSV_STATE_FINISH)
    return FALSE;

  self->current_value_start_ofs = self->current_value_start_pos - self->input;
  g_string_assign(self->current_value, self->current_value_start_pos);
  self->src = self->current_value_start_pos + self->current_value->len;
  self->state = CSV_STATE_GREEDY_COLUMN;
  _translate_value(self);
  return TRUE;
}

/************************************************************************
 * SIMD fast path
 *
 * Activates when the CSVScannerOptions are simple enough that the
 * classifier from csv-scanner-simd.h can represent the record faithfully:
 *   - default ',' delimiter (no `delimiters`, no `string_delimiters`)
 *   - default `"`/`'` quoting (no custom `quotes_start`)
 *   - no `null_value` translation, no escape dialect,
 *     no GREEDY
 * Additionally bails if the record contains `'` anywhere, because the
 * classifier only tracks `"` in its quotes mask.
 *
 * On success, one SIMD pass builds an array of (start, end, quoted) field
 * offsets; subsequent scan_next calls dequeue from that array.  Bails out
 * to the generic path on cases the classifier's view of quoting can't
 * express (unterminated quoted field, literal content after closing quote
 * like "foo"bar). */


/* Trim whitespace from all unquoted fields in the fast path using SIMD. */
static void
_trim_fast_path_fields_whitespace(CSVScanner *self)
{
  for (guint i = 0; i < self->fast_path_fields->len; i++)
    {
      CSVFieldOfs *f = &g_array_index(self->fast_path_fields, CSVFieldOfs, i);
      if (f->quoted)
        continue;  /* Don't trim quoted fields */

      f->start_ofs = csv_simd_trim_leading_whitespace(self->input, f->start_ofs, f->end_ofs);
      f->end_ofs = csv_simd_trim_trailing_whitespace(self->input, f->start_ofs, f->end_ofs);
    }
}

static gboolean
_can_use_simd_fast_path(CSVScanner *self)
{
  CSVScannerOptions *o = self->options;

  if (o->delimiters != NULL)
    return FALSE;

  if (o->string_delimiters != NULL)
    return FALSE;

  if (o->quotes_start != NULL)
    return FALSE;

  if (o->null_value != NULL)
    return FALSE;

  if (o->dialect != CSV_SCANNER_ESCAPE_NONE)
    return FALSE;

  if (o->flags & CSV_SCANNER_GREEDY)
    return FALSE;

  return TRUE;
}

static gboolean
_parse_all_fields_simd(CSVScanner *self)
{
  return csv_simd_parse(self->input, self->input_end - self->input, self->fast_path_fields);
}

static gboolean
_scan_next_from_fast_path(CSVScanner *self)
{
  if (self->fast_path_current_field >= self->fast_path_fields_len)
    {
      self->src = self->input_end;
      self->state = self->expected_columns == 0 ? CSV_STATE_FINISH : CSV_STATE_PARTIAL_INPUT;
      return FALSE;
    }

  /* Use cached raw pointer + length (vs. g_array_index() indirection through ->data) */
  CSVFieldOfs *f = &self->fast_path_fields_data[self->fast_path_current_field++];

  self->current_value_start_pos = self->input + f->start_ofs - (f->quoted ? 1 : 0);
  self->current_value_start_ofs = f->start_ofs;

  /* Defer copying: store offset; csv_scanner_get_current_value() copies on demand.
   * No need to truncate current_value here — _switch_to_next_column() already did
   * that on entry to csv_scanner_scan_next(). */
  self->deferred_field_start = f->start_ofs;
  self->deferred_field_len = f->end_ofs - f->start_ofs;

  /* `src` is only inspected by csv_scanner_is_scan_complete() / has_input_left() /
   * append_rest, all of which give the right answer if it's left at any
   * pre-input_end position during iteration.  We update it once when fields
   * run out (above) — no per-field maintenance. */
  return TRUE;
}

/* Activate the SIMD fast path for this scanner if possible.  Idempotent.
 * After this returns, `self->fast_path_active` reflects whether subsequent
 * scan_next calls will use the fast path. */
static void
_try_activate_fast_path(CSVScanner *self)
{
  if (self->fast_path_tried)
    return;

  self->fast_path_tried = TRUE;
  self->fast_path_active = _can_use_simd_fast_path(self);
  self->fast_path_active = self->fast_path_active && _parse_all_fields_simd(self);

  if (self->fast_path_active && (self->options->flags & CSV_SCANNER_STRIP_WHITESPACE))
    _trim_fast_path_fields_whitespace(self);

  /* Snapshot the GArray's backing storage now that no more appends will
   * happen for this record — the per-field hot path then avoids the
   * GArray indirection entirely. */
  if (self->fast_path_active)
    {
      self->fast_path_fields_data = (CSVFieldOfs *) self->fast_path_fields->data;
      self->fast_path_fields_len = (gint) self->fast_path_fields->len;
    }
}

gint
csv_scanner_peek_field_count(CSVScanner *self)
{
  _try_activate_fast_path(self);
  return self->fast_path_active ? self->fast_path_fields_len : -1;
}

gboolean
csv_scanner_scan_next(CSVScanner *self)
{
  if (!_switch_to_next_column(self))
    return FALSE;

  _try_activate_fast_path(self);

  if (self->fast_path_active)
    return _scan_next_from_fast_path(self);

  self->current_value_start_pos = self->src;

  if (_is_last_column(self) && (self->options->flags & CSV_SCANNER_GREEDY))
    {
      return _take_rest(self);
    }
  else if (self->src[0] == 0)
    {
      /* no more input data and a real column, not a greedy one */
      self->state = self->expected_columns == 0 ? CSV_STATE_FINISH : CSV_STATE_PARTIAL_INPUT;
      return FALSE;
    }
  else
    {
      _parse_opening_quote_character(self);
      _parse_left_whitespace(self);
      _parse_value_with_whitespace_and_delimiter(self);
      _translate_value(self);
      return TRUE;
    }
}

void
csv_scanner_init(CSVScanner *scanner, CSVScannerOptions *options, const gchar *input)
{
  memset(scanner, 0, sizeof(*scanner));
  scanner->state = CSV_STATE_INITIAL;
  scanner->input = scanner->src = input;
  scanner->input_end = input + strlen(input);
  scanner->current_value = scratch_buffers_alloc();
  scanner->current_value_start_pos = NULL;
  scanner->current_column = 0;
  scanner->options = options;

  /* Pre-allocate fast_path_fields for at least 16 columns to avoid early reallocations */
  scanner->fast_path_fields = g_array_sized_new(FALSE, FALSE, sizeof(CSVFieldOfs), 16);
}

void
csv_scanner_set_expected_columns(CSVScanner *scanner, gint expected_columns)
{
  scanner->expected_columns = expected_columns;

  /* Pre-allocate fast_path_fields to avoid reallocations during SIMD parsing */
  if (expected_columns > 16)
    {
      g_array_unref(scanner->fast_path_fields);
      scanner->fast_path_fields = g_array_sized_new(FALSE, FALSE, sizeof(CSVFieldOfs), expected_columns + 16);
    }
}

void
csv_scanner_deinit(CSVScanner *self)
{
  if (self->fast_path_fields)
    {
      g_array_unref(self->fast_path_fields);
      self->fast_path_fields = NULL;
    }
}

gchar *
csv_scanner_dup_current_value(CSVScanner *self)
{
  return g_strndup(csv_scanner_get_current_value(self),
                   csv_scanner_get_current_value_len(self));
}

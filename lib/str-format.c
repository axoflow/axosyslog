/*
 * Copyright (c) 2002-2011 Balabit
 * Copyright (c) 1998-2011 Balázs Scheidler
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

#include "str-format.h"

#include <string.h>
#include <ctype.h>

static gchar digits[] = "0123456789abcdef";

/* format 64 bit ints */

static inline gint
format_uint64_base10_rev(gchar *result, gsize result_len, gint sign, guint64 value)
{
  gchar *p;
  gboolean negative = FALSE;
  gboolean first = TRUE;

  if (sign && ((gint64) value) < 0)
    {
      value = -((gint64) value);
      negative = TRUE;
    }

  p = result;
  while (first || (result_len > 0 && value > 0))
    {
      *p = digits[value % 10];
      value /= 10;
      p++;
      result_len--;
      first = FALSE;
    }
  if (negative && result_len > 0)
    {
      *p = '-';
      p++;
    }
  return p - result;
}

static inline gint
format_uint64_base16_rev(gchar *result, gsize result_len, guint64 value)
{
  gchar *p;

  p = result;
  while (result_len > 0 && value > 0)
    {
      *p = digits[value & 0x0F];
      value >>= 4;
      p++;
      result_len--;
    }
  return p - result;
}

static inline gint
format_uint64_base8_rev(gchar *result, gsize result_len, guint64 value)
{
  gchar *p;
  gboolean first = TRUE;

  p = result;
  while (first || (result_len > 0 && value > 0))
    {
      *p = digits[value & 0x07];
      value >>= 3;
      p++;
      result_len--;
      first = FALSE;
    }
  return p - result;
}

/* simple buffer output */

static gint
format_padded_int64_into_buffer(gchar *result, gsize result_len,
                                gint field_len, gchar pad_char, gint sign, gint base,
                                guint64 value)
{
  gchar num[32];
  gint len, i;

  switch (base)
    {
    case 10:
      len = format_uint64_base10_rev(num, sizeof(num), sign, value);
      break;
    case 16:
      len = format_uint64_base16_rev(num, sizeof(num), value);
      break;
    case 8:
      len = format_uint64_base8_rev(num, sizeof(num), value);
      break;
    default:
      g_assert_not_reached();
    }

  if (field_len == 0 || field_len < len)
    field_len = len;

  /* reverse the characters */
  memset(result, pad_char, field_len - len);
  for (i = 0; i < len; i++)
    {
      result[field_len - i - 1] = num[i];
    }
  if (field_len < result_len)
    result[field_len] = 0;
  else
    result[result_len - 1] = 0;
  return field_len;
}

gint
format_uint64_into_padded_buffer(gchar *result, gsize result_len,
                                 gint field_len, gchar pad_char, gint base,
                                 guint64 value)
{
  return format_padded_int64_into_buffer(result, result_len, field_len, pad_char, 0, base, value);
}

gint
format_int64_into_padded_buffer(gchar *result, gsize result_len,
                                gint field_len, gchar pad_char, gint base,
                                gint64 value)
{
  return format_padded_int64_into_buffer(result, result_len, field_len, pad_char, TRUE, base, value);
}

gint
format_uint32_into_padded_buffer(gchar *result, gsize result_len,
                                 gint field_len, gchar pad_char, gint base,
                                 guint32 value)
{
  return format_padded_int64_into_buffer(result, result_len, field_len, pad_char, 0, base, (guint64) value);
}

gint
format_int32_into_padded_buffer(gchar *result, gsize result_len,
                                gint field_len, gchar pad_char, gint base,
                                gint32 value)
{
  return format_padded_int64_into_buffer(result, result_len, field_len, pad_char, TRUE, base, (gint64) value);
}

/* GString output, these functions _append_ to an existing GString */

static void
format_padded_int64(GString *result,
                    gint field_len, gchar pad_char, gint sign, gint base,
                    guint64 value)
{
  gsize pos = result->len;
  if (G_UNLIKELY(result->allocated_len < result->len + 20))
    {
      /* reserve 20 characters */
      g_string_set_size(result, result->len + 20);
    }
  /* NOTE: result->len can be changed by the g_string_set_size() call */
  gint len = format_padded_int64_into_buffer(&result->str[pos], result->allocated_len - result->len,
                                             field_len, pad_char,
                                             sign, base, value);
  result->len = pos + len;
}

void
format_uint64_padded(GString *result,
                     gint field_len, gchar pad_char, gint base,
                     guint64 value)
{
  format_padded_int64(result, field_len, pad_char, 0, base, value);
}


void
format_int64_padded(GString *result,
                    gint field_len, gchar pad_char, gint base,
                    gint64 value)
{
  format_padded_int64(result, field_len, pad_char, 1, base, value);
}

/* format 32 bit ints */

void
format_uint32_padded(GString *result,
                     gint field_len, gchar pad_char, gint base,
                     guint32 value)
{
  format_padded_int64(result, field_len, pad_char, 0, base, (guint64) value);
}

void
format_int32_padded(GString *result,
                    gint field_len, gchar pad_char, gint base,
                    gint32 value)
{
  format_padded_int64(result, field_len, pad_char, 1, base, (gint64) value);
}

gchar *
format_hex_string_with_delimiter(gconstpointer data, gsize data_len, gchar *result, gsize result_len, gchar delimiter)
{
  gint i;
  gint pos = 0;
  guchar *str = (guchar *) data;

  for (i = 0; i < data_len && result_len - pos >= 3; i++)
    {
      if ( (delimiter != 0) && (i < data_len - 1))
        {
          g_snprintf(result + pos, 4, "%02x%c", str[i], delimiter);
          pos += 3;
        }
      else
        {
          g_snprintf(result + pos, 3, "%02x", str[i]);
          pos += 2;
        }
    }
  return result;
}

gchar *
format_hex_string(gconstpointer data, gsize data_len, gchar *result, gsize result_len)
{
  return format_hex_string_with_delimiter(data, data_len, result, result_len, 0);
}

/* parse 32 bit ints */

gboolean
scan_positive_int(const gchar **buf, gint *left, gint field_width, gint *num)
{
  const gchar *original_buf = *buf;
  gint original_left = *left;
  guint32 result;

  result = 0;

  while (*left > 0 && field_width > 0 && **buf == ' ')
    {
      (*buf)++;
      (*left)--;
      field_width--;
    }

  while (*left > 0 && field_width > 0 &&
         (**buf) >= '0' && (**buf) <= '9')
    {
      result = result * 10 + ((**buf) - '0');
      (*buf)++;
      (*left)--;
      field_width--;
    }
  if (field_width != 0)
    goto fail;
  *num = result;
  return TRUE;

fail:
  *buf = original_buf;
  *left = original_left;
  return FALSE;
}

gboolean
scan_expect_char(const gchar **buf, gint *left, gchar value)
{
  if (*left == 0)
    return FALSE;
  if ((**buf) != value)
    return FALSE;
  (*buf)++;
  (*left)--;
  return TRUE;
}

gboolean
scan_expect_str(const gchar **buf, gint *left, const gchar *value)
{
  const gchar *original_buf = *buf;
  gint original_left = *left;

  while (*value)
    {
      if (*left == 0 || *value != **buf)
        {
          *buf = original_buf;
          *left = original_left;
          return FALSE;
        }

      (*buf)++;
      (*left)--;
      value++;
    }

  return TRUE;
}

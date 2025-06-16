/*
 * Copyright (c) 2021 Balazs Scheidler <bazsi77@gmail.com>
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
 */

#ifndef SYSLOG_NG_GENERIC_NUMBER_H_INCLUDED
#define SYSLOG_NG_GENERIC_NUMBER_H_INCLUDED

#include "syslog-ng.h"

enum
{
  GN_INT64,
  GN_DOUBLE,
  GN_NAN,
};

typedef struct _GenericNumber
{
  union
  {
    gint64 raw_int64;
    gdouble raw_double;
  } value;
  guint8 type, precision;
} GenericNumber;

static inline gdouble
gn_as_double(const GenericNumber *number)
{
  if (number->type == GN_INT64)
    return (gdouble) number->value.raw_int64;
  if (number->type == GN_DOUBLE)
    return number->value.raw_double;
  g_assert_not_reached();
}

static inline void
gn_set_double(GenericNumber *number, gdouble value, gint precision)
{
  number->type = GN_DOUBLE;
  number->value.raw_double = value;
  number->precision = precision > 0 ? precision : 20;
}

gint64 _gn_as_int64(const GenericNumber *number);

static inline gint64
gn_as_int64(const GenericNumber *number)
{
  if (number->type == GN_INT64)
    return number->value.raw_int64;
  return _gn_as_int64(number);
}

static inline void
gn_set_int64(GenericNumber *number, gint64 value)
{
  number->type = GN_INT64;
  number->value.raw_int64 = value;
  number->precision = 0;
}

gboolean _gn_is_zero(const GenericNumber *number);

static inline gboolean
gn_is_zero(const GenericNumber *number)
{
  if (number->type == GN_INT64)
    return number->value.raw_int64 == 0;
  return _gn_is_zero(number);
}

static inline void
gn_set_nan(GenericNumber *number)
{
  number->type = GN_NAN;
}

gboolean _gn_is_nan(const GenericNumber *number);

static inline gboolean
gn_is_nan(const GenericNumber *number)
{
  if (number->type == GN_NAN)
    return TRUE;
  return _gn_is_nan(number);
}

static inline gint
_gn_compare_int64(gint64 l, gint64 r)
{
  if (l == r)
    return 0;
  else if (l < r)
    return -1;

  return 1;
}

gint _gn_compare_double(gdouble l, gdouble r);

static inline gint
gn_compare(const GenericNumber *left, const GenericNumber *right)
{
  if (left->type == right->type)
    {
      if (left->type == GN_INT64)
        return _gn_compare_int64(gn_as_int64(left), gn_as_int64(right));
      else if (left->type == GN_DOUBLE)
        return _gn_compare_double(gn_as_double(left), gn_as_double(right));
    }
  else if (left->type == GN_NAN || right->type == GN_NAN)
    {
      ;
    }
  else if (left->type == GN_DOUBLE || right->type == GN_DOUBLE)
    {
      return _gn_compare_double(gn_as_double(left), gn_as_double(right));
    }
  else
    {
      return _gn_compare_int64(gn_as_int64(left), gn_as_int64(right));
    }
  /* NaNs cannot be compared */
  g_assert_not_reached();
}

#endif

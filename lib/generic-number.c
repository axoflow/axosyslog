/*
 * Copyright (c) 2021 Balazs Scheidler <bazsi77@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 */
#include "generic-number.h"
#include <math.h>

gint64
_gn_as_int64(const GenericNumber *number)
{
  if (number->type == GN_DOUBLE)
    {
      double r = round(number->value.raw_double);

      if (r <= (double) G_MININT64)
        return G_MININT64;
      if (r >= (double) G_MAXINT64)
        return G_MAXINT64;
      return (gint64) r;
    }
  g_assert_not_reached();
}

gboolean
_gn_is_zero(const GenericNumber *number)
{
  if (number->type == GN_DOUBLE)
    return fabs(number->value.raw_double) < DBL_EPSILON;

  g_assert_not_reached();
}

gboolean
_gn_is_nan(const GenericNumber *number)
{
  return (number->type == GN_DOUBLE && isnan(number->value.raw_double));
}

gint
_gn_compare_double(gdouble l, gdouble r)
{
  if (fabs(l - r) < DBL_EPSILON)
    return 0;
  else if (l < r)
    return -1;

  return 1;
}

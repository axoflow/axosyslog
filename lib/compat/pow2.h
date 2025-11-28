/*
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#ifndef COMPAT_POW2_H_INCLUDED
#define COMPAT_POW2_H_INCLUDED

#if (defined(__clang__) || defined(__GNUC__))
static inline int
round_to_log2(size_t x)
{
  if (x <= 1)
    return 0;

  /* __builtin_clzll(): Returns the number of leading 0-bits in X, starting
   * at the most significant bit position.  If X is 0, the result is
   * undefined.
   */
  return (int) sizeof(x) * 8 - __builtin_clzll(x-1);
}
#else
#error "Only gcc/clang is supported to compile this code"
#endif

static inline size_t
pow2(int bits)
{
  return 1 << bits;
}

#endif

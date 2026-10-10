/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Mate Ory <mate.ory@axoflow.com>
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

#include "filterx/func-random.h"
#include "filterx/object-primitive.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-eval.h"
#include "filterx/expr-function.h"
#include "tls-support.h"

#include <openssl/rand.h>

/*
 * Non-cryptographic, per-thread xoroshiro128** generator, seeded from
 * RAND_bytes() on first use in each thread. It needs no locking and no
 * syscalls on the hot path, unlike RAND_bytes() itself.
 */

TLS_BLOCK_START
{
  guint64 random_state[2];
}
TLS_BLOCK_END;

#define random_state __tls_deref(random_state)

static inline guint64
_rotate_left(guint64 x, gint k)
{
  return (x << k) | (x >> (64 - k));
}

static inline gboolean
_is_seeded(void)
{
  /* xoroshiro must not be seeded with all zeroes (it would stay zero forever), so that marks the unseeded state */
  return random_state[0] != 0 || random_state[1] != 0;
}

static void
_seed(void)
{
  if (RAND_bytes((guchar *) random_state, sizeof(random_state)) != 1)
    {
      /* weak fallback, still distinct per thread and per run */
      random_state[0] = (guint64) g_get_real_time() ^ (guint64) GPOINTER_TO_SIZE(&random_state);
      random_state[1] = (guint64) g_get_monotonic_time();
    }

  if (!_is_seeded())
    random_state[0] = 1;
}

static guint64
_next(void)
{
  if (G_UNLIKELY(!_is_seeded()))
    _seed();

  guint64 s0 = random_state[0];
  guint64 s1 = random_state[1];
  /* xoroshiro128** by Blackman & Vigna: "**" output scrambler, then the a=24, b=16, c=37 state step */
  guint64 result = _rotate_left(s0 * 5, 7) * 9;

  s1 ^= s0;
  random_state[0] = _rotate_left(s0, 24) ^ s1 ^ (s1 << 16);
  random_state[1] = _rotate_left(s1, 37);

  return result;
}

/* uniform in [0, bound), without modulo bias */
static guint64
_next_bounded(guint64 bound)
{
  /* -bound % bound == 2^64 % bound: rejecting r below it leaves a range that is a multiple of bound */
  guint64 threshold = -bound % bound;

  for (;;)
    {
      guint64 r = _next();
      if (r >= threshold)
        return r % bound;
    }
}

FilterXObject *
filterx_simple_function_random(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  /* the top 53 bits fill the double mantissa exactly, scaled by 2^-53 into [0, 1) */
  if (args_len == 0)
    return filterx_double_new((_next() >> 11) * 0x1.0p-53);

  if (args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires zero or one argument");
      return NULL;
    }

  gint64 bound;
  if (!filterx_object_extract_integer(args[0], &bound) || bound <= 0)
    {
      filterx_simple_function_argument_error(s, "Argument must be a positive integer");
      return NULL;
    }

  return filterx_integer_new((gint64) _next_bounded((guint64) bound));
}

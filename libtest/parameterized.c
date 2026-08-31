/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 László Várady
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

#include "parameterized.h"

#include <criterion/alloc.h>

static void
_free_params(struct criterion_test_params *crp)
{
  cr_free(crp->params);
}

struct criterion_test_params
static_parameterized_test_params(gsize n_params)
{
  gsize *indexes = cr_malloc(n_params * sizeof(gsize));

  for (gsize i = 0; i < n_params; i++)
    indexes[i] = i;

  return cr_make_param_array(gsize, indexes, n_params, _free_params);
}

/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "func-dict-to-pairs.h"

typedef struct FilterXFunctionDictToPairs_
{
  FilterXFunction super;
} FilterXFunctionDictToPairs;

static FilterXObject *
_dict_to_pairs_eval(FilterXExpr *s)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  FilterXObject *result = NULL;
  return result;
}

static gboolean
_dict_to_pairs_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_dict_to_pairs_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  filterx_function_deinit_method(&self->super, cfg);
}

static FilterXExpr *
_dict_to_pairs_optimize(FilterXExpr *s)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  return filterx_function_optimize_method(&self->super);
}

static void
_dict_to_pairs_free(FilterXExpr *s)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  filterx_function_free_method(&self->super);
}

static gboolean
_extract_dict_to_pairs_args(FilterXFunctionDictToPairs *self, FilterXFunctionArgs *args, GError **error)
{
  return TRUE;
}

FilterXExpr *
filterx_function_dict_to_pairs_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionDictToPairs *self = g_new0(FilterXFunctionDictToPairs, 1);
  filterx_function_init_instance(&self->super, "dict_to_pairs");

  self->super.super.init = _dict_to_pairs_init;
  self->super.super.deinit = _dict_to_pairs_deinit;
  self->super.super.optimize = _dict_to_pairs_optimize;
  self->super.super.eval = _dict_to_pairs_eval;
  self->super.super.free_fn = _dict_to_pairs_free;

  if (!_extract_dict_to_pairs_args(self, args, error) || !filterx_function_args_check(args, error))
    {
      filterx_function_args_free(args);
      filterx_expr_unref(&self->super.super);
      return NULL;
    }

  filterx_function_args_free(args);
  return &self->super.super;
}

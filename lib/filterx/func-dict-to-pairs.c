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
#include "object-string.h"

#define FILTERX_FUNC_DICT_TO_PAIRS_USAGE "Usage: dict_to_pairs(dict, \"key_name\", \"value_name\")"

typedef struct FilterXFunctionDictToPairs_
{
  FilterXFunction super;
  FilterXExpr *dict_expr;
  FilterXObject *key_name_object;
  FilterXObject *value_name_object;
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

  if (!filterx_expr_init(self->dict_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_dict_to_pairs_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  filterx_expr_deinit(self->dict_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static FilterXExpr *
_dict_to_pairs_optimize(FilterXExpr *s)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  self->dict_expr = filterx_expr_optimize(self->dict_expr);
  return filterx_function_optimize_method(&self->super);
}

static void
_dict_to_pairs_free(FilterXExpr *s)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  filterx_expr_unref(self->dict_expr);
  filterx_object_unref(self->key_name_object);
  filterx_object_unref(self->value_name_object);
  filterx_function_free_method(&self->super);
}

static gboolean
_extract_dict_to_pairs_args(FilterXFunctionDictToPairs *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 3)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "dict_to_pairs() requires exactly three arguments. " FILTERX_FUNC_DICT_TO_PAIRS_USAGE);
      return FALSE;
    }

  self->dict_expr = filterx_function_args_get_expr(args, 0);
  g_assert(self->dict_expr);

  gsize key_name_len;
  const gchar *key_name_str = filterx_function_args_get_literal_string(args, 1, &key_name_len);
  if (!key_name_str)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "dict_to_pairs() requires the second argument to be a literal string. "
                  FILTERX_FUNC_DICT_TO_PAIRS_USAGE);
      return FALSE;
    }
  self->key_name_object = filterx_string_new(key_name_str, (gssize) key_name_len);

  gsize value_name_len;
  const gchar *value_name_str = filterx_function_args_get_literal_string(args, 2, &value_name_len);
  if (!value_name_str)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "dict_to_pairs() requires the third argument to be a literal string. "
                  FILTERX_FUNC_DICT_TO_PAIRS_USAGE);
      return FALSE;
    }
  self->value_name_object = filterx_string_new(value_name_str, (gssize) value_name_len);

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

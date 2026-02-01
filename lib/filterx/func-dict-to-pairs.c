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
#include "filterx-sequence.h"
#include "filterx-mapping.h"
#include "object-dict.h"
#include "object-list.h"
#include "object-string.h"
#include "filterx-eval.h"

#define FILTERX_FUNC_DICT_TO_PAIRS_USAGE "Usage: dict_to_pairs(dict, \"key_name\", \"value_name\")"

typedef struct FilterXFunctionDictToPairs_
{
  FilterXFunction super;
  FilterXExpr *dict_expr;
  FilterXObject *key_name_object;
  FilterXObject *value_name_object;
} FilterXFunctionDictToPairs;

static gboolean
_add_pair_to_list(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *)((gpointer *)user_data)[0];
  FilterXObject *list = (FilterXObject *)((gpointer *)user_data)[1];

  gboolean success = FALSE;

  FilterXObject *pair = filterx_dict_sized_new(2);
  if (!pair)
    {
      filterx_eval_push_error_static_info("Failed to add pair to list", &self->super.super,
                                          "Failed to create dict for pair");
      goto exit;
    }

  if (!filterx_object_setattr(pair, self->key_name_object, &key))
    {
      filterx_eval_push_error_static_info("Failed to add pair to list", &self->super.super,
                                          "Failed to add key to pair");
      goto exit;
    }

  if (!filterx_object_setattr(pair, self->value_name_object, &value))
    {
      filterx_eval_push_error_static_info("Failed to add pair to list", &self->super.super,
                                          "Failed to add value to pair");
      goto exit;
    }

  if (!filterx_sequence_append(list, &pair))
    {
      filterx_eval_push_error_static_info("Failed to add pair to list", &self->super.super,
                                          "Failed to append pair to list");
      goto exit;
    }

  success = TRUE;

exit:
  filterx_object_unref(pair);
  return success;
}

static FilterXObject *
_dict_to_pairs_eval(FilterXExpr *s)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  FilterXObject *result = NULL;

  FilterXObject *dict_obj_ref = filterx_expr_eval_typed(self->dict_expr);
  FilterXObject *dict_obj = filterx_ref_unwrap_ro(dict_obj_ref);
  if (!dict_obj)
    {
      filterx_eval_push_error_static_info("Failed to evaluate dict_to_pairs()", s, "Failed to evaluate dict argument");
      return NULL;
    }

  if (!filterx_object_is_type(dict_obj, &FILTERX_TYPE_NAME(dict)))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate dict_to_pairs()", s,
                                          "dict argument must be a dict, got: %s",
                                          filterx_object_get_type_name(dict_obj_ref));
      goto exit;
    }

  result = filterx_list_new();

  gpointer user_data[] = { self, result };
  if (!filterx_object_iter(dict_obj, _add_pair_to_list, user_data))
    {
      filterx_eval_push_error_static_info("Failed to evaluate dict_to_pairs()", s, "Error during iteration over dict");
      filterx_object_unref(result);
      result = NULL;
      goto exit;
    }

exit:
  filterx_object_unref(dict_obj_ref);
  return result;
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

static gboolean
_dict_to_pairs_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionDictToPairs *self = (FilterXFunctionDictToPairs *) s;

  FilterXExpr **exprs[] = { &self->dict_expr };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_function_dict_to_pairs_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionDictToPairs *self = g_new0(FilterXFunctionDictToPairs, 1);
  filterx_function_init_instance(&self->super, "dict_to_pairs");

  self->super.super.eval = _dict_to_pairs_eval;
  self->super.super.walk_children = _dict_to_pairs_walk;
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

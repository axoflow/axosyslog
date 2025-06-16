/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
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
 *
 */

#include "expr-regexp-search.h"
#include "filterx/expr-regexp.h"
#include "filterx/expr-literal.h"
#include "filterx/object-primitive.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-list.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict.h"
#include "filterx/object-dict-interface.h"
#include "filterx/expr-function.h"
#include "filterx/expr-regexp-common.h"
#include "compat/pcre.h"
#include "scratch-buffers.h"

DEFINE_FUNC_FLAG_NAMES(FilterXRegexpSearchFlags,
                       FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO_NAME,
                       FILTERX_REGEXP_SEARCH_LIST_MODE_NAME
                      );

#define FILTERX_FUNC_REGEXP_SEARCH_USAGE "Usage: regexp_search(string, pattern, " \
FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO_NAME"=(boolean), "\
FILTERX_REGEXP_SEARCH_LIST_MODE_NAME"=(boolean))"

typedef struct FilterXExprRegexpSearch_
{
  FilterXFunction super;
  FilterXExpr *lhs;
  pcre2_code_8 *pattern;
  FilterXExpr *pattern_expr;
  FLAGSET flags;
} FilterXExprRegexpSearch;

static FilterXObject *
_store_matches_to_list(pcre2_code_8 *pattern, const FilterXReMatchState *state)
{
  guint32 num_matches = pcre2_get_ovector_count(state->match_data);
  PCRE2_SIZE *matches = pcre2_get_ovector_pointer(state->match_data);
  FilterXObject *result = filterx_list_new();

  for (gint i = 0; i < num_matches; i++)
    {
      if (num_matches > 1 && i==0 && !check_flag(state->flags, FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO))
        continue;
      PCRE2_SIZE begin_index = matches[2 * i];
      PCRE2_SIZE end_index = matches[2 * i + 1];
      if (begin_index == PCRE2_UNSET || end_index == PCRE2_UNSET)
        {
          FilterXObject *null = filterx_null_new();
          filterx_list_append(result, &null);
          continue;
        }

      FILTERX_STRING_DECLARE_ON_STACK(value, state->lhs_str + begin_index, end_index - begin_index);
      gboolean success = filterx_list_append(result, &value);
      filterx_object_unref(value);

      if (!success)
        {
          msg_error("FilterX: Failed to append regexp match to list", evt_tag_int("index", i));
          goto error;
        }
    }

  return result;
error:
  filterx_object_unref(result);
  return NULL;
}

static FilterXObject *
_store_matches_to_dict(pcre2_code_8 *pattern, const FilterXReMatchState *state)
{
  PCRE2_SIZE *matches = pcre2_get_ovector_pointer(state->match_data);
  guint32 num_matches = pcre2_get_ovector_count(state->match_data);
  gchar num_str_buf[G_ASCII_DTOSTR_BUF_SIZE];
  FilterXObject *result = filterx_dict_new();

  /* First store all matches with string formatted indexes as keys. */
  for (guint32 i = 0; i < num_matches; i++)
    {
      if (num_matches > 1 && i==0 && !check_flag(state->flags, FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO))
        continue;

      PCRE2_SIZE begin_index = matches[2 * i];
      PCRE2_SIZE end_index = matches[2 * i + 1];
      if (begin_index == PCRE2_UNSET || end_index == PCRE2_UNSET)
        continue;

      g_snprintf(num_str_buf, sizeof(num_str_buf), "%" G_GUINT32_FORMAT, i);
      FILTERX_STRING_DECLARE_ON_STACK(key, num_str_buf, -1);
      FILTERX_STRING_DECLARE_ON_STACK(value, state->lhs_str + begin_index, end_index - begin_index);

      gboolean success = filterx_object_set_subscript(result, key, &value);

      filterx_object_unref(key);
      filterx_object_unref(value);

      if (!success)
        {
          msg_error("FilterX: Failed to add regexp match to dict", evt_tag_str("key", num_str_buf));
          goto error;
        }
    }

  gchar *name_table = NULL;
  guint32 name_entry_size = 0;
  guint32 namecount = 0;
  pcre2_pattern_info(pattern, PCRE2_INFO_NAMETABLE, &name_table);
  pcre2_pattern_info(pattern, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);
  pcre2_pattern_info(pattern, PCRE2_INFO_NAMECOUNT, &namecount);

  /* Rename named matches. */
  for (guint32 i = 0; i < namecount; i++, name_table += name_entry_size)
    {
      int n = (name_table[0] << 8) | name_table[1];
      PCRE2_SIZE begin_index = matches[2 * n];
      PCRE2_SIZE end_index = matches[2 * n + 1];
      const gchar *namedgroup_name = name_table + 2;

      if (begin_index < 0 || end_index < 0)
        continue;

      g_snprintf(num_str_buf, sizeof(num_str_buf), "%" G_GUINT32_FORMAT, n);
      FILTERX_STRING_DECLARE_ON_STACK(num_key, num_str_buf, -1);
      FILTERX_STRING_DECLARE_ON_STACK(key, namedgroup_name, -1);
      FilterXObject *value = filterx_object_get_subscript(result, num_key);

      gboolean success = filterx_object_set_subscript(result, key, &value);
      g_assert(filterx_object_unset_key(result, num_key));

      filterx_object_unref(key);
      filterx_object_unref(num_key);
      filterx_object_unref(value);

      if (!success)
        {
          msg_error("FilterX: Failed to add regexp match to dict", evt_tag_str("key", namedgroup_name));
          goto error;
        }
    }

  return result;
error:
  filterx_object_unref(result);
  return NULL;
}

static FilterXObject *
_eval_regexp_search(FilterXExpr *s)
{
  FilterXExprRegexpSearch *self = (FilterXExprRegexpSearch *) s;
  FilterXObject *result = NULL;
  FilterXReMatchState state;

  filterx_expr_rematch_state_init(&state);
  state.flags = self->flags;

  gboolean success = filterx_regexp_match_eval(self->lhs, self->pattern, &state);
  if (!success)
    {
      /* not match, return empty dict */
      if (check_flag(self->flags, FILTERX_REGEXP_SEARCH_LIST_MODE))
        result = filterx_list_new();
      else
        result = filterx_dict_new();
      goto exit;
    }

  if (!state.match_data)
    goto exit;

  if (check_flag(self->flags, FILTERX_REGEXP_SEARCH_LIST_MODE))
    result = _store_matches_to_list(self->pattern, &state);
  else
    result = _store_matches_to_dict(self->pattern, &state);

exit:
  filterx_expr_rematch_state_cleanup(&state);
  return result;
}

static FilterXExpr *
_regexp_search_optimize(FilterXExpr *s)
{
  FilterXExprRegexpSearch *self = (FilterXExprRegexpSearch *) s;

  self->lhs = filterx_expr_optimize(self->lhs);
  self->pattern_expr = filterx_expr_optimize(self->pattern_expr);
  return NULL;
}

static gboolean
_regexp_search_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXExprRegexpSearch *self = (FilterXExprRegexpSearch *) s;

  FilterXObject *pattern_obj = NULL;

  if (!filterx_expr_init(self->lhs, cfg))
    goto error;

  if (!filterx_expr_init(self->pattern_expr, cfg))
    goto error;

  if (!filterx_expr_is_literal(self->pattern_expr))
    {
      msg_error("regexp_search(): pattern argument must be a literal string. " FILTERX_FUNC_REGEXP_SEARCH_USAGE);
      goto error;
    }

  pattern_obj = filterx_expr_eval(self->pattern_expr);

  gsize pattern_len;
  const gchar *pattern = filterx_string_get_value_ref(pattern_obj, &pattern_len);
  if (!pattern)
    {
      msg_error("regexp_search(): pattern argument must be a literal string. " FILTERX_FUNC_REGEXP_SEARCH_USAGE);
      goto error;
    }

  self->pattern = filterx_regexp_compile_pattern_defaults(pattern);
  if (!self->pattern)
    {
      msg_error("regexp_search(): failed to compile pattern. " FILTERX_FUNC_REGEXP_SEARCH_USAGE);
      goto error;
    }

  return filterx_expr_init_method(s, cfg);

error:
  filterx_object_unref(pattern_obj);
  filterx_expr_deinit(self->lhs, cfg);
  filterx_expr_deinit(self->pattern_expr, cfg);
  return FALSE;
}

static void
_regexp_search_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXExprRegexpSearch *self = (FilterXExprRegexpSearch *) s;

  filterx_expr_deinit(self->lhs, cfg);
  filterx_expr_deinit(self->pattern_expr, cfg);
  filterx_expr_deinit_method(s, cfg);
}

static void
_regexp_search_free(FilterXExpr *s)
{
  FilterXExprRegexpSearch *self = (FilterXExprRegexpSearch *) s;

  filterx_expr_unref(self->lhs);
  filterx_expr_unref(self->pattern_expr);
  if (self->pattern)
    pcre2_code_free(self->pattern);
  filterx_function_free_method(&self->super);
}

static gboolean
_extract_optional_arg_flag(FilterXExprRegexpSearch *self, FilterXRegexpSearchFlags flag,
                           FilterXFunctionArgs *args, GError **error)
{
  return filterx_regexp_extract_optional_arg_flag(&self->flags, FilterXRegexpSearchFlags_NAMES,
                                                  FilterXRegexpSearchFlags_MAX, flag, FILTERX_FUNC_REGEXP_SEARCH_USAGE, args, error);
}

static gboolean
_extract_search_args(FilterXExprRegexpSearch *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_REGEXP_SEARCH_USAGE);
      return FALSE;
    }

  self->lhs = filterx_function_args_get_expr(args, 0);
  self->pattern_expr = filterx_function_args_get_expr(args, 1);

  return TRUE;
}

/* Takes reference of lhs */
FilterXExpr *
filterx_function_regexp_search_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXExprRegexpSearch *self = g_new0(FilterXExprRegexpSearch, 1);

  filterx_function_init_instance(&self->super, "regexp_search");
  self->super.super.eval = _eval_regexp_search;
  self->super.super.optimize = _regexp_search_optimize;
  self->super.super.init = _regexp_search_init;
  self->super.super.deinit = _regexp_search_deinit;
  self->super.super.free_fn = _regexp_search_free;

  if (!_extract_optional_arg_flag(self, FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO, args, error))
    goto error;

  if (!_extract_optional_arg_flag(self, FILTERX_REGEXP_SEARCH_LIST_MODE, args, error))
    goto error;

  if (!_extract_search_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

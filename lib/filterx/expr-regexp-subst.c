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

#include "expr-regexp-subst.h"
#include "filterx/expr-regexp.h"
#include "filterx/object-primitive.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/expr-function.h"
#include "filterx/expr-regexp-common.h"
#include "compat/pcre.h"
#include "scratch-buffers.h"
#include <ctype.h>

DEFINE_FUNC_FLAG_NAMES(FilterXRegexpSubstFlags,
                       FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT_NAME,
                       FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL_NAME,
                       FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8_NAME,
                       FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE_NAME,
                       FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE_NAME,
                       FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS_NAME
                      );

#define FILTERX_FUNC_REGEXP_SUBST_USAGE "Usage: regexp_subst(string, pattern, replacement, " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE_NAME"=(boolean)" \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS_NAME"=(boolean))" \

#define FILTERX_FUNC_REGEXP_SUBST_GRP_ID_MAX_DIGITS 3

typedef struct FilterXFuncRegexpSubst_
{
  FilterXFunction super;
  FilterXExpr *string_expr;
  pcre2_code_8 *pattern;
  gchar *replacement;
  FLAGSET flags;
} FilterXFuncRegexpSubst;

static gchar *
_next_matchgrp_ref(gchar *from, gchar **to)
{
  if (from == NULL || *from == '\0')
    return NULL;
  g_assert(to);
  while (*from != '\0')
    {
      if ((*from == '\\') && isdigit(*(from + 1)))
        {
          gchar *start = from;
          from += 2;
          while (isdigit(*from) && from - start <= FILTERX_FUNC_REGEXP_SUBST_GRP_ID_MAX_DIGITS)
            {
              from++;
            }
          *to = from;
          return start;
        }
      from++;
    }
  return NULL;
}

static gboolean
_parse_machgrp_ref(const gchar *from, const gchar *to, gint *value)
{
  if (!from || !to || !value || from >= to || to > from + 5)
    {
      return FALSE;
    }

  if (*from != '\\')
    {
      return FALSE;
    }

  from++;
  *value = 0;

  while (from < to && isdigit(*from))
    {
      *value = (*value * 10) + (*from - '0');
      from++;
    }

  return from == to;
}

static gboolean
_build_replacement_stirng_with_match_groups(const FilterXFuncRegexpSubst *self, FilterXReMatchState *state,
                                            GString *replacement_string)
{
  g_string_set_size(replacement_string, 0);
  gint num_grps = state->rc;
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(state->match_data);

  gchar *pos = self->replacement;
  gchar *last = pos;
  gchar *close = NULL;
  gint idx = -1;
  while ((pos = _next_matchgrp_ref(pos, &close)) != NULL)
    {
      if (_parse_machgrp_ref(pos, close, &idx) && (idx < num_grps))
        {
          PCRE2_SIZE start = ovector[2 * idx];
          PCRE2_SIZE end = ovector[2 * idx + 1];
          if (start != PCRE2_UNSET)
            {
              g_string_append_len(replacement_string, last, pos - last);
              last = close;
              size_t group_len = end - start;
              g_string_append_len(replacement_string, state->lhs_str + start, group_len);
            }
        }
      pos = close;
    }
  g_string_append_len(replacement_string, last, pos - last);
  return TRUE;
}

static FilterXObject *
_replace_matches(const FilterXFuncRegexpSubst *self, FilterXReMatchState *state)
{
  GString *new_value = scratch_buffers_alloc();
  PCRE2_SIZE *ovector = NULL;
  gint pos = 0;
  const gchar *replacement_string = self->replacement;

  if (check_flag(self->flags, FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS))
    {
      GString *rep_str = scratch_buffers_alloc();
      _build_replacement_stirng_with_match_groups(self, state, rep_str);
      replacement_string = rep_str->str;
    }
  do
    {
      ovector = pcre2_get_ovector_pointer(state->match_data);

      g_string_append_len(new_value, state->lhs_str + pos, match_start_offset(ovector) - pos);
      g_string_append(new_value, replacement_string);

      if (is_zero_length_match(ovector))
        {
          g_string_append_len(new_value, state->lhs_str + pos, 1);
          pos++;
        }
      else
        pos = match_end_offset(ovector);

      if (!filterx_regexp_match(state, self->pattern, pos))
        break;
    }
  while ((pos < state->lhs_str_len) && check_flag(self->flags, FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL));

  // add the rest of the string
  g_string_append_len(new_value, state->lhs_str + pos, state->lhs_str_len - pos);

  // handle the very last of zero lenght matches
  if (is_zero_length_match(ovector))
    g_string_append(new_value, replacement_string);

  return filterx_string_new(new_value->str, new_value->len);
}

static FilterXObject *
_subst_eval(FilterXExpr *s)
{
  FilterXFuncRegexpSubst *self = (FilterXFuncRegexpSubst *) s;

  FilterXObject *result = NULL;
  FilterXReMatchState state;
  filterx_expr_rematch_state_init(&state);

  gboolean matched = filterx_regexp_match_eval(self->string_expr, self->pattern, &state);
  if (!matched)
    {
      result = filterx_object_ref(state.lhs_obj);
      goto exit;
    }

  if (!state.match_data)
    {
      /* Error happened during matching. */
      result = NULL;
      goto exit;
    }

  result = _replace_matches(self, &state);

exit:
  filterx_expr_rematch_state_cleanup(&state);
  return result;
}

static FilterXExpr *
_extract_subst_string_expr_arg(FilterXFunctionArgs *args, GError **error)
{
  return filterx_function_args_get_expr(args, 0);
}

static gint
_create_compile_opts(FLAGSET flags)
{
  gboolean utf8 = check_flag(flags, FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8);
  gboolean ignorecase = check_flag(flags, FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE);
  gboolean newline = check_flag(flags, FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE);

  gint res = 0;
  res ^= (-utf8 ^ res) & PCRE2_NO_UTF_CHECK;
  res ^= (-ignorecase ^ res) & PCRE2_CASELESS;
  res ^= (-newline ^ res) & PCRE2_NEWLINE_ANYCRLF;
  return res;
}

static pcre2_code_8 *
_extract_subst_pattern_arg(FilterXFuncRegexpSubst *self, FilterXFunctionArgs *args, GError **error)
{
  const gchar *pattern = filterx_function_args_get_literal_string(args, 1, NULL);
  if (!pattern)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be a string literal: pattern. " FILTERX_FUNC_REGEXP_SUBST_USAGE);
      return NULL;
    }

  return filterx_regexp_compile_pattern(pattern, check_flag(self->flags, FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT),
                                        _create_compile_opts(self->flags));
}

static gchar *
_extract_subst_replacement_arg(FilterXFunctionArgs *args, GError **error)
{
  const gchar *replacement = filterx_function_args_get_literal_string(args, 2, NULL);
  if (!replacement)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be a string literal: replacement. " FILTERX_FUNC_REGEXP_SUBST_USAGE);
      return NULL;
    }

  return g_strdup(replacement);
}

static gboolean
_extract_optional_arg_flag(FilterXFuncRegexpSubst *self, FilterXRegexpSubstFlags flag,
                           FilterXFunctionArgs *args, GError **error)
{
  return filterx_regexp_extract_optional_arg_flag(&self->flags, FilterXRegexpSubstFlags_NAMES,
                                                  FilterXRegexpSubstFlags_MAX, flag, FILTERX_FUNC_REGEXP_SUBST_USAGE, args, error);
}

static gboolean
_extract_optional_flags(FilterXFuncRegexpSubst *self, FilterXFunctionArgs *args, GError **error)
{
  if (!_extract_optional_arg_flag(self, FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL, args, error))
    return FALSE;
  if (!_extract_optional_arg_flag(self, FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT, args, error))
    return FALSE;
  if (!_extract_optional_arg_flag(self, FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE, args, error))
    return FALSE;
  if (!_extract_optional_arg_flag(self, FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE, args, error))
    return FALSE;
  if (!_extract_optional_arg_flag(self, FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8, args, error))
    return FALSE;
  if (!_extract_optional_arg_flag(self, FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS, args, error))
    return FALSE;
  return TRUE;
}

static gboolean
_contains_match_grp_ref(gchar *str)
{
  gchar *close = NULL;
  return _next_matchgrp_ref(str, &close) != NULL;
}

static gboolean
_extract_subst_args(FilterXFuncRegexpSubst *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 3)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_REGEXP_SUBST_USAGE);
      return FALSE;
    }

  self->string_expr = _extract_subst_string_expr_arg(args, error);
  if (!self->string_expr)
    return FALSE;

  if (!_extract_optional_flags(self, args, error))
    return FALSE;

  self->pattern = _extract_subst_pattern_arg(self, args, error);
  if (!self->pattern)
    return FALSE;

  self->replacement = _extract_subst_replacement_arg(args, error);
  if (!self->replacement)
    return FALSE;
  // turn off group mode if there is no match grp ref due to it's performance impact
  if (!_contains_match_grp_ref(self->replacement))
    set_flag(&self->flags, FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS, FALSE);

  return TRUE;
}

static FilterXExpr *
_subst_optimize(FilterXExpr *s)
{
  FilterXFuncRegexpSubst *self = (FilterXFuncRegexpSubst *) s;

  self->string_expr = filterx_expr_optimize(self->string_expr);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_subst_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFuncRegexpSubst *self = (FilterXFuncRegexpSubst *) s;

  if (!filterx_expr_init(self->string_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_subst_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFuncRegexpSubst *self = (FilterXFuncRegexpSubst *) s;
  filterx_expr_deinit(self->string_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_subst_free(FilterXExpr *s)
{
  FilterXFuncRegexpSubst *self = (FilterXFuncRegexpSubst *) s;
  filterx_expr_unref(self->string_expr);
  if (self->pattern)
    pcre2_code_free(self->pattern);
  g_free(self->replacement);
  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_function_regexp_subst_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFuncRegexpSubst *self = g_new0(FilterXFuncRegexpSubst, 1);
  filterx_function_init_instance(&self->super, "regexp_subst");
  self->super.super.eval = _subst_eval;
  self->super.super.optimize = _subst_optimize;
  self->super.super.init = _subst_init;
  self->super.super.deinit = _subst_deinit;
  self->super.super.free_fn = _subst_free;

  reset_flags(&self->flags, FLAG_VAL(FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT) | FLAG_VAL(
                FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS));
  if (!_extract_subst_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

gboolean
filterx_regexp_subst_is_jit_enabled(FilterXExpr *s)
{
  g_assert(s);
  FilterXFuncRegexpSubst *self = (FilterXFuncRegexpSubst *)s;
  PCRE2_SIZE jit_size;
  int info_result = pcre2_pattern_info(self->pattern, PCRE2_INFO_JITSIZE, &jit_size);
  return info_result == 0 && jit_size > 0;
}

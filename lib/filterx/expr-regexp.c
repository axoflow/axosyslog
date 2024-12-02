/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/expr-regexp.h"
#include "filterx/object-primitive.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/expr-function.h"
#include "filterx/filterx-object-istype.h"
#include "filterx/filterx-ref.h"
#include "compat/pcre.h"
#include "scratch-buffers.h"
#include "filterx/expr-regexp-common.h"

#define FILTERX_FUNC_REGEXP_SUBST_USAGE "Usage: regexp_subst(string, pattern, replacement, " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE_NAME"=(boolean)" \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS_NAME"=(boolean))" \

typedef struct FilterXExprRegexpMatch_
{
  FilterXExpr super;
  FilterXExpr *lhs;
  pcre2_code_8 *pattern;
  gboolean invert;
} FilterXExprRegexpMatch;

static FilterXObject *
_regexp_match_eval(FilterXExpr *s)
{
  FilterXExprRegexpMatch *self = (FilterXExprRegexpMatch *) s;

  FilterXObject *result = NULL;
  FilterXReMatchState state;
  filterx_expr_rematch_state_init(&state);

  gboolean matched = filterx_regexp_match_eval(self->lhs, self->pattern, &state);
  if (!state.match_data)
    {
      /* Error happened during matching. */
      goto exit;
    }

  result = filterx_boolean_new(matched != self->invert);

exit:
  filterx_expr_rematch_state_cleanup(&state);
  return result;
}

static gboolean
_regexp_match_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXExprRegexpMatch *self = (FilterXExprRegexpMatch *) s;

  if (!filterx_expr_init(self->lhs, cfg))
    return FALSE;

  return filterx_expr_init_method(s, cfg);
}

static void
_regexp_match_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXExprRegexpMatch *self = (FilterXExprRegexpMatch *) s;

  filterx_expr_deinit(self->lhs, cfg);
  filterx_expr_deinit_method(s, cfg);
}

static void
_regexp_match_free(FilterXExpr *s)
{
  FilterXExprRegexpMatch *self = (FilterXExprRegexpMatch *) s;

  filterx_expr_unref(self->lhs);
  if (self->pattern)
    pcre2_code_free(self->pattern);
  filterx_expr_free_method(s);
}

/* Takes reference of lhs */
FilterXExpr *
filterx_expr_regexp_match_new(FilterXExpr *lhs, const gchar *pattern)
{
  FilterXExprRegexpMatch *self = g_new0(FilterXExprRegexpMatch, 1);

  filterx_expr_init_instance(&self->super);
  self->super.eval = _regexp_match_eval;
  self->super.init = _regexp_match_init;
  self->super.deinit = _regexp_match_deinit;
  self->super.free_fn = _regexp_match_free;

  self->lhs = lhs;
  self->pattern = filterx_regexp_compile_pattern_defaults(pattern);
  if (!self->pattern)
    {
      filterx_expr_unref(&self->super);
      return NULL;
    }

  return &self->super;
}

FilterXExpr *
filterx_expr_regexp_nomatch_new(FilterXExpr *lhs, const gchar *pattern)
{
  FilterXExprRegexpMatch *self = (FilterXExprRegexpMatch *)filterx_expr_regexp_match_new(lhs, pattern);
  self->invert = TRUE;
  return &self->super;
}

typedef struct FilterXFuncRegexpSubst_
{
  FilterXFunction super;
  FilterXExpr *string_expr;
  pcre2_code_8 *pattern;
  gchar *replacement;
  FilterXFuncRegexpSubstOpts opts;
} FilterXFuncRegexpSubst;


static inline gint
_start_offset(PCRE2_SIZE *ovector)
{
  return ovector[0];
}

static inline gint
_end_offset(PCRE2_SIZE *ovector)
{
  return ovector[1];
}

static inline gboolean
_is_zero_length_match(PCRE2_SIZE *ovector)
{
  return ovector[0] == ovector[1];
}

static gboolean
_build_replacement_stirng_with_match_groups(const FilterXFuncRegexpSubst *self, FilterXReMatchState *state,
                                            GString *replacement_string)
{
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(state->match_data);
  g_string_set_size(replacement_string, 0);
  const gchar *rep_ptr = self->replacement;
  const gchar *last_ptr = rep_ptr;
  gint num_grps = state->rc;

  while (*rep_ptr)
    {
      if (*rep_ptr == '\\')
        {
          rep_ptr++;
          if (*rep_ptr >= '1' && *rep_ptr <= '9')
            {
              gint grp_idx = *rep_ptr - '0';
              if (grp_idx < num_grps)
                {
                  PCRE2_SIZE start = ovector[2 * grp_idx];
                  PCRE2_SIZE end = ovector[2 * grp_idx + 1];
                  if (start != PCRE2_UNSET)
                    {
                      g_string_append_len(replacement_string, last_ptr, rep_ptr - last_ptr - 1);
                      last_ptr = rep_ptr + 1;
                      size_t group_len = end - start;
                      g_string_append_len(replacement_string, state->lhs_str + start, group_len);
                    }
                }
            }
          rep_ptr++;
        }
      else
        rep_ptr++;
    }
  g_string_append_len(replacement_string, last_ptr, rep_ptr - last_ptr);
  return TRUE;
}

static FilterXObject *
_replace_matches(const FilterXFuncRegexpSubst *self, FilterXReMatchState *state)
{
  GString *new_value = scratch_buffers_alloc();
  PCRE2_SIZE *ovector = NULL;
  gint pos = 0;
  const gchar *replacement_string = self->replacement;

  if (self->opts.groups)
    {
      GString *rep_str = scratch_buffers_alloc();
      _build_replacement_stirng_with_match_groups(self, state, rep_str);
      replacement_string = rep_str->str;
    }

  do
    {
      ovector = pcre2_get_ovector_pointer(state->match_data);

      g_string_append_len(new_value, state->lhs_str + pos, _start_offset(ovector) - pos);
      g_string_append(new_value, replacement_string);

      if (_is_zero_length_match(ovector))
        {
          g_string_append_len(new_value, state->lhs_str + pos, 1);
          pos++;
        }
      else
        pos = _end_offset(ovector);

      if (!filterx_regexp_match(state, self->pattern, pos))
        break;
    }
  while ((pos < state->lhs_str_len) && self->opts.global);

  // add the rest of the string
  g_string_append_len(new_value, state->lhs_str + pos, state->lhs_str_len - pos);

  // handle the very last of zero lenght matches
  if (_is_zero_length_match(ovector))
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
_create_compile_opts(FilterXFuncRegexpSubstOpts opts)
{
  gint res = 0;
  res ^= (-opts.utf8 ^ res) & PCRE2_NO_UTF_CHECK;
  res ^= (-opts.ignorecase ^ res) & PCRE2_CASELESS;
  res ^= (-opts.newline ^ res) & PCRE2_NEWLINE_ANYCRLF;
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

  return filterx_regexp_compile_pattern(pattern, self->opts.jit, _create_compile_opts(self->opts));
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
_extract_literal_bool(FilterXFunctionArgs *args, const gchar *option_name, gboolean *value, GError **error)
{
  gboolean exists, eval_error;
  gboolean val = filterx_function_args_get_named_literal_boolean(args, option_name, &exists, &eval_error);
  if (exists)
    {
      if (eval_error)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      "%s argument must be boolean literal. ", option_name);
          return FALSE;
        };
      *value = val;
    }
  return TRUE;
}

static gboolean
_extract_optional_flags(FilterXFuncRegexpSubst *self, FilterXFunctionArgs *args, GError **error)
{
  if (!_extract_literal_bool(args, FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL_NAME,
                             &self->opts.global, error))
    return FALSE;
  if (!_extract_literal_bool(args, FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT_NAME, &self->opts.jit,
                             error))
    return FALSE;
  if (!_extract_literal_bool(args, FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE_NAME,
                             &self->opts.ignorecase, error))
    return FALSE;
  if (!_extract_literal_bool(args, FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE_NAME,
                             &self->opts.newline, error))
    return FALSE;
  if (!_extract_literal_bool(args, FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8_NAME, &self->opts.utf8,
                             error))
    return FALSE;
  if (!_extract_literal_bool(args, FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS_NAME,
                             &self->opts.groups, error))
    return FALSE;
  return TRUE;
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


  return TRUE;
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

static void
_opts_init(FilterXFuncRegexpSubstOpts *opts)
{
  memset(opts, 0, sizeof(FilterXFuncRegexpSubstOpts));
  opts->jit = TRUE;
}

FilterXExpr *
filterx_function_regexp_subst_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFuncRegexpSubst *self = g_new0(FilterXFuncRegexpSubst, 1);
  filterx_function_init_instance(&self->super, "regexp_subst");
  self->super.super.eval = _subst_eval;
  self->super.super.init = _subst_init;
  self->super.super.deinit = _subst_deinit;
  self->super.super.free_fn = _subst_free;

  _opts_init(&self->opts);

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

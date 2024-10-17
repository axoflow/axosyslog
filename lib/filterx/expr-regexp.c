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

#define FILTERX_FUNC_REGEXP_SUBST_USAGE "Usage: regexp_subst(string, pattern, replacement, " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE_NAME"=(boolean) " \
  FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE_NAME"=(boolean))" \

#define FILTERX_FUNC_REGEXP_SEARCH_USAGE "Usage: regexp_search(string, pattern)"

typedef struct FilterXReMatchState_
{
  pcre2_match_data *match_data;
  FilterXObject *lhs_obj;
  const gchar *lhs_str;
  gsize lhs_str_len;
} FilterXReMatchState;

static void
_state_init(FilterXReMatchState *state)
{
  memset(state, 0, sizeof(FilterXReMatchState));
}

static void
_state_cleanup(FilterXReMatchState *state)
{
  if (state->match_data)
    pcre2_match_data_free(state->match_data);
  filterx_object_unref(state->lhs_obj);
  memset(state, 0, sizeof(FilterXReMatchState));
}

static pcre2_code_8 *
_compile_pattern(const gchar *pattern, gboolean jit_enabled, gint opts)
{
  gint rc;
  PCRE2_SIZE error_offset;
  gint flags = opts | PCRE2_DUPNAMES;

  pcre2_code_8 *compiled = pcre2_compile((PCRE2_SPTR) pattern, PCRE2_ZERO_TERMINATED, flags, &rc, &error_offset, NULL);
  if (!compiled)
    {
      PCRE2_UCHAR error_message[128];
      pcre2_get_error_message(rc, error_message, sizeof(error_message));
      msg_error("FilterX: Failed to compile regexp pattern",
                evt_tag_str("pattern", pattern),
                evt_tag_str("error", (const gchar *) error_message),
                evt_tag_int("error_offset", (gint) error_offset));
      return NULL;
    }

  if (jit_enabled)
    {
      rc = pcre2_jit_compile(compiled, PCRE2_JIT_COMPLETE);
      if (rc < 0)
        {
          PCRE2_UCHAR error_message[128];
          pcre2_get_error_message(rc, error_message, sizeof(error_message));
          msg_debug("FilterX: Failed to JIT compile regular expression",
                    evt_tag_str("pattern", pattern),
                    evt_tag_str("error", (const gchar *) error_message));
        }
    }

  return compiled;
}

static pcre2_code_8 *
_compile_pattern_defaults(const gchar *pattern)
{
  return _compile_pattern(pattern, TRUE, 0);
}

static gboolean
_match_inner(FilterXReMatchState *state, pcre2_code_8 *pattern, gint start_offset)
{
  gint rc = pcre2_match(pattern, (PCRE2_SPTR) state->lhs_str, (PCRE2_SIZE) state->lhs_str_len, (PCRE2_SIZE) start_offset,
                        0,
                        state->match_data, NULL);
  if (rc < 0)
    {
      switch (rc)
        {
        case PCRE2_ERROR_NOMATCH:
          return FALSE;
        default:
          /* Handle other special cases */
          msg_error("FilterX: Error while matching regexp", evt_tag_int("error_code", rc));
          goto error;
        }
    }
  else if (rc == 0)
    {
      msg_error("FilterX: Error while storing matching substrings, more than 256 capture groups encountered");
      goto error;
    }

  return TRUE;

error:
  return FALSE;
}

/*
 * Returns whether lhs matched the pattern.
 * Populates state if no error happened.
 */
static gboolean
_match(FilterXExpr *lhs_expr, pcre2_code_8 *pattern, FilterXReMatchState *state)
{
  state->lhs_obj = filterx_expr_eval(lhs_expr);
  if (!state->lhs_obj)
    goto error;

  if (!filterx_object_extract_string_ref(state->lhs_obj, &state->lhs_str, &state->lhs_str_len))
    {
      msg_error("FilterX: Regexp matching left hand side must be string type",
                evt_tag_str("type", state->lhs_obj->type->name));
      goto error;
    }

  state->match_data = pcre2_match_data_create_from_pattern(pattern, NULL);
  return _match_inner(state, pattern, 0);
error:
  _state_cleanup(state);
  return FALSE;
}

static gboolean
_has_named_capture_groups(pcre2_code_8 *pattern)
{
  guint32 namecount = 0;
  pcre2_pattern_info(pattern, PCRE2_INFO_NAMECOUNT, &namecount);
  return namecount > 0;
}

static gboolean
_store_matches_to_list(pcre2_code_8 *pattern, const FilterXReMatchState *state, FilterXObject *fillable)
{
  guint32 num_matches = pcre2_get_ovector_count(state->match_data);
  PCRE2_SIZE *matches = pcre2_get_ovector_pointer(state->match_data);

  for (gint i = 0; i < num_matches; i++)
    {
      gint begin_index = matches[2 * i];
      gint end_index = matches[2 * i + 1];
      if (begin_index < 0 || end_index < 0)
        continue;

      FilterXObject *value = filterx_string_new(state->lhs_str + begin_index, end_index - begin_index);
      gboolean success = filterx_list_append(fillable, &value);
      filterx_object_unref(value);

      if (!success)
        {
          msg_error("FilterX: Failed to append regexp match to list", evt_tag_int("index", i));
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
_store_matches_to_dict(pcre2_code_8 *pattern, const FilterXReMatchState *state, FilterXObject *fillable)
{
  PCRE2_SIZE *matches = pcre2_get_ovector_pointer(state->match_data);
  guint32 num_matches = pcre2_get_ovector_count(state->match_data);
  gchar num_str_buf[G_ASCII_DTOSTR_BUF_SIZE];

  /* First store all matches with string formatted indexes as keys. */
  for (guint32 i = 0; i < num_matches; i++)
    {
      PCRE2_SIZE begin_index = matches[2 * i];
      PCRE2_SIZE end_index = matches[2 * i + 1];
      if (begin_index < 0 || end_index < 0)
        continue;

      g_snprintf(num_str_buf, sizeof(num_str_buf), "%" G_GUINT32_FORMAT, i);
      FilterXObject *key = filterx_string_new(num_str_buf, -1);
      FilterXObject *value = filterx_string_new(state->lhs_str + begin_index, end_index - begin_index);

      gboolean success = filterx_object_set_subscript(fillable, key, &value);

      filterx_object_unref(key);
      filterx_object_unref(value);

      if (!success)
        {
          msg_error("FilterX: Failed to add regexp match to dict", evt_tag_str("key", num_str_buf));
          return FALSE;
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
      FilterXObject *num_key = filterx_string_new(num_str_buf, -1);
      FilterXObject *key = filterx_string_new(namedgroup_name, -1);
      FilterXObject *value = filterx_object_get_subscript(fillable, num_key);

      gboolean success = filterx_object_set_subscript(fillable, key, &value);
      g_assert(filterx_object_unset_key(fillable, num_key));

      filterx_object_unref(key);
      filterx_object_unref(num_key);
      filterx_object_unref(value);

      if (!success)
        {
          msg_error("FilterX: Failed to add regexp match to dict", evt_tag_str("key", namedgroup_name));
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
_store_matches(pcre2_code_8 *pattern, const FilterXReMatchState *state, FilterXObject *fillable)
{
  fillable = filterx_ref_unwrap_rw(fillable);

  if (filterx_object_is_type(fillable, &FILTERX_TYPE_NAME(list)))
    return _store_matches_to_list(pattern, state, fillable);

  if (filterx_object_is_type(fillable, &FILTERX_TYPE_NAME(dict)))
    return _store_matches_to_dict(pattern, state, fillable);

  msg_error("FilterX: Failed to store regexp match data, invalid fillable type",
            evt_tag_str("type", fillable->type->name));
  return FALSE;
}


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
  _state_init(&state);

  gboolean matched = _match(self->lhs, self->pattern, &state);
  if (!state.match_data)
    {
      /* Error happened during matching. */
      goto exit;
    }

  result = filterx_boolean_new(matched != self->invert);

exit:
  _state_cleanup(&state);
  return result;
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
  self->super.free_fn = _regexp_match_free;

  self->lhs = lhs;
  self->pattern = _compile_pattern_defaults(pattern);
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

typedef struct FilterXExprRegexpSearchGenerator_
{
  FilterXGeneratorFunction super;
  FilterXExpr *lhs;
  pcre2_code_8 *pattern;
} FilterXExprRegexpSearchGenerator;

static gboolean
_regexp_search_generator_generate(FilterXExprGenerator *s, FilterXObject *fillable)
{
  FilterXExprRegexpSearchGenerator *self = (FilterXExprRegexpSearchGenerator *) s;

  gboolean result;
  FilterXReMatchState state;
  _state_init(&state);

  gboolean matched = _match(self->lhs, self->pattern, &state);
  if (!matched)
    {
      result = TRUE;
      goto exit;
    }

  if (!state.match_data)
    {
      /* Error happened during matching. */
      result = FALSE;
      goto exit;
    }

  result = _store_matches(self->pattern, &state, fillable);

exit:
  _state_cleanup(&state);
  return result;
}

static FilterXObject *
_regexp_search_generator_create_container(FilterXExprGenerator *s, FilterXExpr *fillable_parent)
{
  FilterXExprRegexpSearchGenerator *self = (FilterXExprRegexpSearchGenerator *) s;

  if (_has_named_capture_groups(self->pattern))
    return filterx_generator_create_dict_container(s, fillable_parent);

  return filterx_generator_create_list_container(s, fillable_parent);
}

static void
_regexp_search_generator_free(FilterXExpr *s)
{
  FilterXExprRegexpSearchGenerator *self = (FilterXExprRegexpSearchGenerator *) s;

  filterx_expr_unref(self->lhs);
  if (self->pattern)
    pcre2_code_free(self->pattern);
  filterx_generator_function_free_method(&self->super);
}

static gboolean
_extract_search_args(FilterXExprRegexpSearchGenerator *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_REGEXP_SEARCH_USAGE);
      return FALSE;
    }

  self->lhs = filterx_function_args_get_expr(args, 0);

  const gchar *pattern = filterx_function_args_get_literal_string(args, 1, NULL);
  if (!pattern)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "pattern must be string literal. " FILTERX_FUNC_REGEXP_SEARCH_USAGE);
      return FALSE;
    }

  self->pattern = _compile_pattern_defaults(pattern);
  if (!self->pattern)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "failed to compile pattern. " FILTERX_FUNC_REGEXP_SEARCH_USAGE);
      return FALSE;
    }

  return TRUE;

}

/* Takes reference of lhs */
FilterXExpr *
filterx_generator_function_regexp_search_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXExprRegexpSearchGenerator *self = g_new0(FilterXExprRegexpSearchGenerator, 1);

  filterx_generator_function_init_instance(&self->super, "regexp_search");
  self->super.super.generate = _regexp_search_generator_generate;
  self->super.super.super.free_fn = _regexp_search_generator_free;
  self->super.super.create_container = _regexp_search_generator_create_container;

  if (!_extract_search_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super.super);
  return NULL;
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

static FilterXObject *
_replace_matches(const FilterXFuncRegexpSubst *self, FilterXReMatchState *state)
{
  GString *new_value = scratch_buffers_alloc();
  PCRE2_SIZE *ovector = NULL;
  gint pos = 0;
  do
    {
      ovector = pcre2_get_ovector_pointer(state->match_data);

      g_string_append_len(new_value, state->lhs_str + pos, _start_offset(ovector) - pos);
      g_string_append(new_value, self->replacement);

      if (_is_zero_length_match(ovector))
        {
          g_string_append_len(new_value, state->lhs_str + pos, 1);
          pos++;
        }
      else
        pos = _end_offset(ovector);

      if (!_match_inner(state, self->pattern, pos))
        break;
    }
  while ((pos < state->lhs_str_len) && self->opts.global);

  // add the rest of the string
  g_string_append_len(new_value, state->lhs_str + pos, state->lhs_str_len - pos);

  // handle the very last of zero lenght matches
  if (_is_zero_length_match(ovector))
    g_string_append(new_value, self->replacement);

  return filterx_string_new(new_value->str, new_value->len);
}

static FilterXObject *
_subst_eval(FilterXExpr *s)
{
  FilterXFuncRegexpSubst *self = (FilterXFuncRegexpSubst *) s;

  FilterXObject *result = NULL;
  FilterXReMatchState state;
  _state_init(&state);

  gboolean matched = _match(self->string_expr, self->pattern, &state);
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
  _state_cleanup(&state);
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

  return _compile_pattern(pattern, self->opts.jit, _create_compile_opts(self->opts));
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

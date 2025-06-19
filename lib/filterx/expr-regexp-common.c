/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
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

#include "expr-regexp-common.h"
#include "filterx/object-extractor.h"

pcre2_code_8 *
filterx_regexp_compile_pattern(const gchar *pattern, gboolean jit_enabled, gint opts)
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

pcre2_code_8 *
filterx_regexp_compile_pattern_defaults(const gchar *pattern)
{
  return filterx_regexp_compile_pattern(pattern, TRUE, 0);
}

void
filterx_expr_rematch_state_init(FilterXReMatchState *state)
{
  state->match_data = NULL;
  state->lhs_obj = NULL;
}

void
filterx_expr_rematch_state_cleanup(FilterXReMatchState *state)
{
  if (state->match_data)
    pcre2_match_data_free(state->match_data);
  filterx_object_unref(state->lhs_obj);
}

gboolean
filterx_regexp_extract_optional_arg_flag(FLAGSET *flags, const gchar **flag_names, guint64 flags_max, FLAGSET flag,
                                         const gchar *usage,
                                         FilterXFunctionArgs *args, GError **error)
{
  g_assert(flags);
  gboolean exists, eval_error;
  g_assert(flag < flags_max);
  const gchar *arg_name = flag_names[flag];
  gboolean value = filterx_function_args_get_named_literal_boolean(args, arg_name, &exists, &eval_error);
  if (!exists)
    return TRUE;

  if (eval_error)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "%s argument must be boolean literal. %s", arg_name, usage);
      return FALSE;
    }

  set_flag(flags, flag, value);

  return TRUE;
}

gboolean
filterx_regexp_match(FilterXReMatchState *state, pcre2_code_8 *pattern, gint start_offset)
{
  gint rc = pcre2_match(pattern, (PCRE2_SPTR) state->lhs_str, (PCRE2_SIZE) state->lhs_str_len, (PCRE2_SIZE) start_offset,
                        0,
                        state->match_data, NULL);
  state->rc = rc;
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
gboolean
filterx_regexp_match_eval(FilterXExpr *lhs_expr, pcre2_code_8 *pattern, FilterXReMatchState *state)
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
  return filterx_regexp_match(state, pattern, 0);
error:
  return FALSE;
}

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

#ifndef FILTERX_EXPR_REGEXP_COMMON_H_INCLUDED
#define FILTERX_EXPR_REGEXP_COMMON_H_INCLUDED

#include "compat/pcre.h"
#include "filterx/object-primitive.h"
#include "filterx/func-flags.h"
#include "filterx/expr-function.h"

typedef struct FilterXReMatchState_
{
  pcre2_match_data *match_data;
  FilterXObject *lhs_obj;
  const gchar *lhs_str;
  gsize lhs_str_len;
  gint rc;
  FLAGSET flags;
} FilterXReMatchState;

pcre2_code_8 *filterx_regexp_compile_pattern(const gchar *pattern, gboolean jit_enabled, gint opts);
pcre2_code_8 *filterx_regexp_compile_pattern_defaults(const gchar *pattern);

void filterx_expr_rematch_state_init(FilterXReMatchState *state);
void filterx_expr_rematch_state_cleanup(FilterXReMatchState *state);

gboolean filterx_regexp_extract_optional_arg_flag(FLAGSET *flags, const gchar **flag_names, guint64 flags_max,
                                                  FLAGSET flag, const gchar *usage, FilterXFunctionArgs *args, GError **error);

gboolean filterx_regexp_match(FilterXReMatchState *state, pcre2_code_8 *pattern, gint start_offset);
gboolean filterx_regexp_match_eval(FilterXExpr *lhs_expr, pcre2_code_8 *pattern, FilterXReMatchState *state);

static inline gint
match_start_offset(PCRE2_SIZE *ovector)
{
  return ovector[0];
}

static inline gint
match_end_offset(PCRE2_SIZE *ovector)
{
  return ovector[1];
}

static inline gboolean
is_zero_length_match(PCRE2_SIZE *ovector)
{
  return ovector[0] == ovector[1];
}

#endif

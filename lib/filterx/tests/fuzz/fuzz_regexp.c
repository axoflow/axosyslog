/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

/* The input is split at the first newline: the bytes before it are the
 * pattern, the rest is the subject to match. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <glib.h>

#include "apphook.h"
#include "cfg.h"
#include "scratch-buffers.h"
#include "logmsg/logmsg.h"
#include "filterx/expr-regexp.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-scope.h"

static GlobalConfig *fuzz_cfg;

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  app_startup();
  fuzz_cfg = cfg_new_snippet();
  return 0;
}

static void
_drive(const gchar *pattern, const gchar *subject, gssize subject_len, gboolean invert)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_string_new(subject, subject_len));
  FilterXExpr *expr = invert
                      ? filterx_expr_regexp_nomatch_new(lhs, pattern)
                      : filterx_expr_regexp_match_new(lhs, pattern);
  if (!expr)
    return;   /* pattern failed to compile; the constructor already freed lhs */

  if (filterx_expr_init(expr, fuzz_cfg))
    {
      FilterXObject *result = filterx_expr_eval(expr);
      filterx_object_unref(result);
      filterx_expr_deinit(expr, fuzz_cfg);
    }
  filterx_expr_unref(expr);
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  /* The match constructor compiles the pattern with strlen(), so it must be a
   * NUL-terminated C string. */
  const uint8_t *nl = memchr(data, '\n', size);
  gsize pattern_len = nl ? (gsize) (nl - data) : size;
  const gchar *subject = nl ? (const gchar *) (nl + 1) : "";
  gssize subject_len = nl ? (gssize) (size - pattern_len - 1) : 0;
  gchar *pattern = g_strndup((const gchar *) data, pattern_len);

  /* filterx_literal_new() freezes its object into the active eval context and
   * evaluation pushes errors there, so establish (and tear down) one per input. */
  LogMessage *msg = log_msg_new_empty();
  FilterXScope *scope = filterx_scope_new(NULL, NULL);
  FilterXEvalContext context;
  filterx_eval_begin_context(&context, NULL, scope, msg);

  _drive(pattern, subject, subject_len, FALSE);
  _drive(pattern, subject, subject_len, TRUE);

  filterx_scope_free(scope);
  filterx_eval_clear_errors();
  filterx_eval_end_context(&context);
  log_msg_unref(msg);
  g_free(pattern);

  scratch_buffers_explicit_gc();   /* keep the per-thread scratch pool bounded across iterations */
  return 0;
}

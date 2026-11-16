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

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <glib.h>

#include "apphook.h"
#include "cfg.h"
#include "scratch-buffers.h"
#include "logmsg/logmsg.h"
#include "filterx-func-parse-cef.h"
#include "filterx-func-parse-leef.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-scope.h"

typedef FilterXExpr *(*FunctionCtor)(FilterXFunctionArgs *args, GError **error);

static GlobalConfig *fuzz_cfg;

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  app_startup();
  fuzz_cfg = cfg_new_snippet();
  return 0;
}

static FilterXFunctionArg *
_string_arg(const gchar *name, const gchar *value, gssize value_len)
{
  return filterx_function_arg_new(name, filterx_literal_new(filterx_string_new(value, value_len)));
}

/* Consumes args_list, also on failure. */
static void
_run(FunctionCtor ctor, GList *args_list)
{
  GError *error = NULL;
  FilterXFunctionArgs *args = filterx_function_args_new(args_list, &error);
  FilterXExpr *func = ctor(args, &error);
  if (error)
    g_error_free(error);

  if (func)
    {
      if (filterx_expr_init(func, fuzz_cfg))
        {
          FilterXObject *result = filterx_expr_eval(func);
          filterx_object_unref(result);
          filterx_expr_deinit(func, fuzz_cfg);
        }
      filterx_expr_unref(func);
    }
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  LogMessage *msg = log_msg_new_empty();
  FilterXScope *scope = filterx_scope_new(NULL, NULL);
  FilterXEvalContext context;
  filterx_eval_begin_context(&context, NULL, scope, msg);

  /* Config 1: whole input, default separators. */
  _run(filterx_function_parse_cef_new, g_list_append(NULL, _string_arg(NULL, (const gchar *) data, (gssize) size)));
  _run(filterx_function_parse_leef_new, g_list_append(NULL, _string_arg(NULL, (const gchar *) data, (gssize) size)));

  /* Config 2: fuzzer-controlled separators. Split at the first two newlines. */
  const uint8_t *nl1 = memchr(data, '\n', size);
  if (nl1)
    {
      gsize vsep_len = (gsize) (nl1 - data);
      const uint8_t *rest = nl1 + 1;
      gsize rest_len = size - vsep_len - 1;
      const uint8_t *nl2 = memchr(rest, '\n', rest_len);
      gsize psep_len = nl2 ? (gsize) (nl2 - rest) : rest_len;
      const uint8_t *payload = nl2 ? nl2 + 1 : rest + rest_len;
      gsize payload_len = nl2 ? rest_len - psep_len - 1 : 0;

      for (gint i = 0; i < 2; i++)
        {
          FunctionCtor ctor = i == 0 ? filterx_function_parse_cef_new : filterx_function_parse_leef_new;
          GList *args = NULL;
          args = g_list_append(args, _string_arg(NULL, (const gchar *) payload, (gssize) payload_len));
          args = g_list_append(args, _string_arg("value_separator", (const gchar *) data, (gssize) vsep_len));
          args = g_list_append(args, _string_arg("pair_separator", (const gchar *) rest, (gssize) psep_len));
          _run(ctor, args);
        }
    }

  filterx_scope_free(scope);
  filterx_eval_clear_errors();
  filterx_eval_end_context(&context);
  log_msg_unref(msg);

  scratch_buffers_explicit_gc();   /* keep the per-thread scratch pool bounded across iterations */
  return 0;
}

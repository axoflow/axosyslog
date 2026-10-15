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
#include <glib.h>

#include "apphook.h"
#include "cfg.h"
#include "scratch-buffers.h"
#include "logmsg/logmsg.h"
#include "filterx-parse-xml.h"
#include "filterx/expr-function.h"
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

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  LogMessage *msg = log_msg_new_empty();
  FilterXScope *scope = filterx_scope_new(NULL, NULL);
  FilterXEvalContext context;
  filterx_eval_begin_context(&context, NULL, scope, msg);

  /* args_new and the ctor consume every ref, also on failure. */
  FilterXExpr *input_expr = filterx_literal_new(filterx_string_new((const gchar *) data, (gssize) size));
  FilterXFunctionArg *input = filterx_function_arg_new(NULL, input_expr);
  GList *args_list = g_list_append(NULL, input);

  GError *error = NULL;
  FilterXFunctionArgs *args = filterx_function_args_new(args_list, &error);
  FilterXExpr *func = filterx_function_parse_xml_new(args, &error);
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

  filterx_scope_free(scope);
  filterx_eval_clear_errors();
  filterx_eval_end_context(&context);
  log_msg_unref(msg);

  scratch_buffers_explicit_gc();   /* keep the per-thread scratch pool bounded across iterations */
  return 0;
}

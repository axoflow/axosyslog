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
#include "filterx-func-format-csv.h"
#include "filterx/json-repr.h"
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

/* filterx_literal_new() takes ownership, so each run needs its own instance. */
static FilterXFunctionArg *
_value_arg(const gchar *json, gssize json_len)
{
  GError *error = NULL;
  FilterXObject *obj = filterx_object_from_json(json, json_len, &error);
  if (error)
    g_error_free(error);
  if (!obj)
    return NULL;
  return filterx_function_arg_new(NULL, filterx_literal_new(obj));
}

static FilterXFunctionArg *
_string_arg(const gchar *name, const gchar *value)
{
  return filterx_function_arg_new(name, filterx_literal_new(filterx_string_new(value, -1)));
}

static FilterXFunctionArg *
_list_arg(const gchar *name, const gchar *json)
{
  GError *error = NULL;
  FilterXObject *list = filterx_object_from_json(json, -1, &error);
  if (error)
    g_error_free(error);
  return filterx_function_arg_new(name, filterx_literal_new(list));
}

/* Consumes args_list, also on failure. */
static void
_run(GList *args_list)
{
  GError *error = NULL;
  FilterXFunctionArgs *args = filterx_function_args_new(args_list, &error);
  FilterXExpr *func = filterx_function_format_csv_new(args, &error);
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

  const gchar *json = (const gchar *) data;
  gssize json_len = (gssize) size;

  FilterXFunctionArg *value;

  if ((value = _value_arg(json, json_len)))
    _run(g_list_append(NULL, value));

  if ((value = _value_arg(json, json_len)))
    {
      GList *args = g_list_append(NULL, value);
      args = g_list_append(args, _string_arg("delimiter", ";"));
      args = g_list_append(args, _string_arg("default_value", "-"));
      _run(args);
    }

  /* Config 3: explicit columns projection (dict-input path). */
  if ((value = _value_arg(json, json_len)))
    {
      GList *args = g_list_append(NULL, value);
      args = g_list_append(args, _list_arg("columns", "[\"a\",\"b\",\"c\"]"));
      _run(args);
    }

  filterx_scope_free(scope);
  filterx_eval_clear_errors();
  filterx_eval_end_context(&context);
  log_msg_unref(msg);

  scratch_buffers_explicit_gc();   /* keep the per-thread scratch pool bounded across iterations */
  return 0;
}

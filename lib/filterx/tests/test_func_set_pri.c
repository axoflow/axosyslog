/*
 * Copyright (c) 2026 Axoflow
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

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "filterx/func-set-pri.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object.h"
#include "logpipe.h"

#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_func_set_pri, null_argument_eval_does_not_crash)
{
  GList *args = g_list_append(NULL,
                              filterx_function_arg_new(NULL, filterx_dummy_error_new("argument failed to evaluate")));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_set_pri_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *res = init_and_eval_expr(func);
  cr_assert_null(res);

  filterx_expr_unref(func);
}

static FilterXExpr *
_create_set_pri(gint64 pri)
{
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL, filterx_literal_new(filterx_integer_new(pri))));

  GError *err = NULL;
  FilterXExpr *func = filterx_function_set_pri_new(filterx_function_args_new(args, NULL), &err);
  cr_assert_null(err);
  return func;
}

/* consumes msg, returns the message the context holds after the eval */
static LogMessage *
_eval_on_message(FilterXExpr *func, LogMessage *msg)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  FilterXEvalContext context;

  FILTERX_EVAL_BEGIN_CONTEXT(context, filterx_eval_get_context(), msg, NULL)
  {
    context.path_options = &path_options;

    FilterXObject *res = init_and_eval_expr(func);
    cr_assert(filterx_object_truthy(res));
    filterx_object_unref(res);

    msg = context.msg;
  }
  FILTERX_EVAL_END_CONTEXT(context);
  return msg;
}

Test(filterx_func_set_pri, writes_unprotected_message_in_place)
{
  LogMessage *msg = log_msg_new_empty();
  FilterXExpr *func = _create_set_pri(10);

  LogMessage *result = _eval_on_message(func, log_msg_ref(msg));
  cr_assert_eq(result, msg);
  cr_assert_eq(msg->pri, 10);

  log_msg_unref(result);
  log_msg_unref(msg);
  filterx_expr_unref(func);
}

Test(filterx_func_set_pri, clones_write_protected_message)
{
  LogMessage *msg = log_msg_new_empty();
  guint16 original_pri = msg->pri;
  cr_assert_neq(original_pri, 10);
  log_msg_write_protect(msg);
  FilterXExpr *func = _create_set_pri(10);

  LogMessage *result = _eval_on_message(func, log_msg_ref(msg));
  cr_assert_neq(result, msg);
  cr_assert_eq(result->pri, 10);
  cr_assert_eq(msg->pri, original_pri);

  log_msg_unref(result);
  log_msg_unref(msg);
  filterx_expr_unref(func);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_func_set_pri, .init = setup, .fini = teardown);

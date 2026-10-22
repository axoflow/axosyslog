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

#include "filterx/func-timestamp.h"
#include "filterx/object-datetime.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object.h"
#include "logpipe.h"

#include "apphook.h"
#include "scratch-buffers.h"

static FilterXExpr *
_create_set_timestamp(gint64 sec)
{
  UnixTime ut = { .ut_sec = sec };
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL, filterx_literal_new(filterx_datetime_new(&ut))));

  GError *err = NULL;
  FilterXExpr *func = filterx_function_set_timestamp_new(filterx_function_args_new(args, NULL), &err);
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

Test(filterx_func_set_timestamp, writes_unprotected_message_in_place)
{
  LogMessage *msg = log_msg_new_empty();
  FilterXExpr *func = _create_set_timestamp(946684800);

  LogMessage *result = _eval_on_message(func, log_msg_ref(msg));
  cr_assert_eq(result, msg);
  cr_assert_eq(msg->timestamps[LM_TS_STAMP].ut_sec, 946684800);

  log_msg_unref(result);
  log_msg_unref(msg);
  filterx_expr_unref(func);
}

Test(filterx_func_set_timestamp, clones_write_protected_message)
{
  LogMessage *msg = log_msg_new_empty();
  gint64 original_sec = msg->timestamps[LM_TS_STAMP].ut_sec;
  cr_assert_neq(original_sec, 946684800);
  log_msg_write_protect(msg);
  FilterXExpr *func = _create_set_timestamp(946684800);

  LogMessage *result = _eval_on_message(func, log_msg_ref(msg));
  cr_assert_neq(result, msg);
  cr_assert_eq(result->timestamps[LM_TS_STAMP].ut_sec, 946684800);
  cr_assert_eq(msg->timestamps[LM_TS_STAMP].ut_sec, original_sec);

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

TestSuite(filterx_func_set_timestamp, .init = setup, .fini = teardown);

/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "syslog-ng.h"
#include "scratch-buffers.h"
#include "apphook.h"
#include "cfg.h"

#include "filterx/filterx-eval.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-compound.h"
#include "filterx/object-primitive.h"
#include "libtest/filterx-lib.h"
#include "libtest/grab-logging.h"

static FilterXExpr *
_create_embedded_exprs(gint depth, gboolean set_location)
{
  g_assert(depth > 0);

  FilterXExpr *fx_root = filterx_dummy_error_new("Dummy error");
  if (set_location)
    filterx_test_expr_set_location_with_text(fx_root, "syslog-ng.conf", 0, 0, 0, 10, "dummy-error-0");

  for (gint i = 0; i < depth - 1; i++)
    {
      fx_root = filterx_non_literal_new_from_expr(fx_root);

      GString *text = scratch_buffers_alloc();
      g_string_printf(text, "dummy-error-%d", i + 1);

      if (set_location)
        filterx_test_expr_set_location_with_text(fx_root, "syslog-ng.conf", i + 1, 0, i + 1, text->len - 1, text->str);
    }
  filterx_expr_init(fx_root, configuration);
  return fx_root;
}

static void
_assert_error_in_logs(gint index)
{
  if (index == 0)
    {
      assert_grabbed_log_contains("FILTERX ERROR; err_idx='[1/8]', expr='syslog-ng.conf:0:0|\t"
                                  "dummy-error-0', error='Dummy error'");
      return;
    }

  GString *error_log = scratch_buffers_alloc();
  g_string_printf(error_log,
                  "FILTERX ERROR; err_idx='[%d/%d]', expr='syslog-ng.conf:%d:0|\t"
                  "dummy-error-%d', error='Failed to evaluate non-literal: Failed to evaluate expression'",
                  index + 1, FILTERX_CONTEXT_ERROR_STACK_SIZE, index, index);
  assert_grabbed_log_contains(error_log->str);
}

Test(filterx_eval, test_filterx_eval_full_error_stack)
{
  LogMessage *msg = log_msg_new_empty();
  FilterXEvalContext eval_context, *prev_eval_context = NULL;

  FILTERX_EVAL_BEGIN_CONTEXT(eval_context, prev_eval_context, msg)
  {
    FilterXExpr *expr = _create_embedded_exprs(FILTERX_CONTEXT_ERROR_STACK_SIZE, TRUE);

    cr_assert_eq(filterx_eval_exec(&eval_context, expr), FXE_FAILURE);

    for (gint i = 0; i < FILTERX_CONTEXT_ERROR_STACK_SIZE; i++)
      _assert_error_in_logs(i);

    filterx_expr_deinit(expr, configuration);
    filterx_expr_unref(expr);
  }
  FILTERX_EVAL_END_CONTEXT(eval_context);

  log_msg_unref(msg);
}

Test(filterx_eval, test_filterx_eval_error_stack_overflow)
{
  LogMessage *msg = log_msg_new_empty();
  FilterXEvalContext eval_context, *prev_eval_context = NULL;

  FILTERX_EVAL_BEGIN_CONTEXT(eval_context, prev_eval_context, msg)
  {
    FilterXExpr *expr = _create_embedded_exprs(FILTERX_CONTEXT_ERROR_STACK_SIZE + 1, TRUE);

    cr_assert_eq(filterx_eval_exec(&eval_context, expr), FXE_FAILURE);

    assert_grabbed_log_contains("FilterX: Reached maximum error stack size.");

    for (gint i = 0; i < FILTERX_CONTEXT_ERROR_STACK_SIZE; i++)
      _assert_error_in_logs(i);

    filterx_expr_deinit(expr, configuration);
    filterx_expr_unref(expr);
  }
  FILTERX_EVAL_END_CONTEXT(eval_context);

  log_msg_unref(msg);
}

Test(filterx_eval, test_filterx_eval_error_stack_location_backfill)
{
  LogMessage *msg = log_msg_new_empty();
  FilterXEvalContext eval_context, *prev_eval_context = NULL;

  FILTERX_EVAL_BEGIN_CONTEXT(eval_context, prev_eval_context, msg)
  {
    FilterXExpr *expr = _create_embedded_exprs(FILTERX_CONTEXT_ERROR_STACK_SIZE, FALSE);

    filterx_test_expr_set_location_with_text(expr, "syslog-ng.conf", 0, 0, 0, 10, "dummy-error");
    cr_assert_eq(filterx_eval_exec(&eval_context, expr), FXE_FAILURE);

    assert_grabbed_log_contains("FILTERX ERROR; err_idx='[1/8]', expr='syslog-ng.conf:0:0|\t"
                                "dummy-error', error='Dummy error'");
    for (gint i = 1; i < FILTERX_CONTEXT_ERROR_STACK_SIZE; i++)
      {
        GString *error_log = scratch_buffers_alloc();
        g_string_printf(error_log,
                        "FILTERX ERROR; err_idx='[%d/%d]', expr='syslog-ng.conf:0:0|\t"
                        "dummy-error', error='Failed to evaluate non-literal: Failed to evaluate expression'",
                        i + 1, FILTERX_CONTEXT_ERROR_STACK_SIZE);
        assert_grabbed_log_contains(error_log->str);
      }

    filterx_expr_deinit(expr, configuration);
    filterx_expr_unref(expr);
  }
  FILTERX_EVAL_END_CONTEXT(eval_context);

  log_msg_unref(msg);
}

static void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
  configuration->log_level = 3;
  cfg_init(configuration);
  start_grabbing_messages();
}

static void
teardown(void)
{
  stop_grabbing_messages();
  cfg_free(configuration);
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(filterx_eval, .init = setup, .fini = teardown);

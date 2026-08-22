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
#include "filterx/expr-variable.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/json-repr.h"
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
      fx_root = filterx_expr_wrapper_new(fx_root);

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
      assert_grabbed_log_contains("FILTERX ERROR; err_idx='[1/32]', expr='syslog-ng.conf:0:0|\t"
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

  FILTERX_EVAL_BEGIN_CONTEXT(eval_context, prev_eval_context, msg, NULL)
  {
    FilterXExpr *expr = _create_embedded_exprs(FILTERX_CONTEXT_ERROR_STACK_SIZE, TRUE);

    cr_assert_eq(filterx_eval_exec(&eval_context, expr, NULL), FXE_FAILURE);

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

  FILTERX_EVAL_BEGIN_CONTEXT(eval_context, prev_eval_context, msg, NULL)
  {
    FilterXExpr *expr = _create_embedded_exprs(FILTERX_CONTEXT_ERROR_STACK_SIZE + 1, TRUE);

    cr_assert_eq(filterx_eval_exec(&eval_context, expr, NULL), FXE_FAILURE);

    assert_grabbed_log_contains("FilterX: Reached maximum error stack size.");

    for (gint i = 0; i < FILTERX_CONTEXT_ERROR_STACK_SIZE; i++)
      _assert_error_in_logs(i);

    filterx_expr_deinit(expr, configuration);
    filterx_expr_unref(expr);
  }
  FILTERX_EVAL_END_CONTEXT(eval_context);

  log_msg_unref(msg);
}

Test(filterx_eval, test_filterx_eval_context_dup_creates_independent_snapshot)
{
  FilterXVariableHandle handles[] = { filterx_map_varname_to_handle("floatvar", FX_VAR_DECLARED_FLOATING) };
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  LogMessage *msg = log_msg_new_empty();
  FilterXEvalContext eval_context, *prev_eval_context = NULL;
  FilterXEvalContext *dup_context = NULL;

  FILTERX_EVAL_BEGIN_CONTEXT(eval_context, prev_eval_context, msg, layout)
  {
    FilterXVariable *v = filterx_scope_register_variable(eval_context.scope, FX_VAR_DECLARED_FLOATING, handles[0], 0);
    FilterXObject *o = filterx_boolean_new(TRUE);
    filterx_scope_set_variable(eval_context.scope, v, &o, TRUE);
    filterx_object_unref(o);

    dup_context = filterx_eval_context_dup(&eval_context);
  }
  FILTERX_EVAL_END_CONTEXT(eval_context);

  /* the context/scope/message used to take the snapshot are all gone by now */
  log_msg_unref(msg);
  filterx_scope_variable_layout_free(layout);

  cr_assert_not_null(dup_context);
  cr_assert_null(dup_context->previous_context);
  cr_assert_null(dup_context->allocator);

  FilterXVariable *dup_var = filterx_scope_lookup_variable(dup_context->scope, handles[0], 0);
  cr_assert_not_null(dup_var);
  cr_assert(filterx_boolean_get_value(dup_var->value));

  filterx_eval_context_free_dup(dup_context);
}

/*
 * A dict-in-a-dict's inner FilterXRef only points *down* to its value via
 * a real (owning) reference; the *up* link back to its enclosing container
 * (parent_container) is a weakref, kept alive only via
 * filterx_eval_store_weak_ref().  If the outer dict is not otherwise
 * reachable (e.g.  only its innermost child ended up in a variable), that
 * weak_refs entry is the *only* thing keeping it alive.
 *
 * filterx_eval_context_dup() must make sure such orphaned ancestors -- newly
 * created while cloning the value into the snapshot -- get their protecting
 * weak_refs entry in the *new* context, not in the one being torn down.
 */
Test(filterx_eval, test_filterx_eval_context_dup_keeps_cloned_parent_containers_alive)
{
  FilterXVariableHandle handles[] = { filterx_map_varname_to_handle("nested", FX_VAR_DECLARED_FLOATING) };
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  LogMessage *msg = log_msg_new_empty();
  FilterXEvalContext eval_context, *prev_eval_context = NULL;
  FilterXEvalContext *dup_context = NULL;

  FILTERX_EVAL_BEGIN_CONTEXT(eval_context, prev_eval_context, msg, layout)
  {
    FilterXObject *r = filterx_object_from_json("{\"c\":{\"cc\":{\"ccc\":\"orig\"}}}", -1, NULL);
    cr_assert(filterx_object_is_ref(r));

    FilterXObject *c = filterx_object_getattr_string(r, "c");
    FilterXObject *cc = filterx_object_getattr_string(c, "cc");
    cr_assert(filterx_object_is_ref(cc));

    /* r and c are not stored anywhere themselves: from here on, their only
     * protection against being freed is whatever filterx_eval_store_weak_ref()
     * did for them when their parent_container linkage was established */
    filterx_object_unref(r);
    filterx_object_unref(c);

    FilterXVariable *v = filterx_scope_register_variable(eval_context.scope, FX_VAR_DECLARED_FLOATING, handles[0], 0);
    filterx_scope_set_variable(eval_context.scope, v, &cc, TRUE);
    filterx_object_unref(cc);

    dup_context = filterx_eval_context_dup(&eval_context);
  }
  FILTERX_EVAL_END_CONTEXT(eval_context);

  /* the original context -- and whatever it was protecting via weak_refs --
   * is now completely gone */
  log_msg_unref(msg);
  filterx_scope_variable_layout_free(layout);

  FilterXVariable *dup_var = filterx_scope_lookup_variable(dup_context->scope, handles[0], 0);
  cr_assert_not_null(dup_var);
  FilterXObject *cc_dup = filterx_variable_get_value(dup_var);
  cr_assert(filterx_object_is_ref(cc_dup));

  /* mutating the deepest clone walks up through its (also cloned) ancestors
   * via parent_container; if those ancestors weren't kept alive
   * independently of the original (now-gone) context, this reads freed
   * memory */
  FilterXObject *new_value = filterx_string_new("changed", -1);
  cr_assert(filterx_object_setattr_string(cc_dup, "ccc", &new_value));
  filterx_object_unref(new_value);

  filterx_object_unref(cc_dup);
  filterx_eval_context_free_dup(dup_context);
}

static void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
  configuration->log_level = 3;
  cfg_init(configuration);
  init_libtest_filterx();
  start_grabbing_messages();
}

static void
teardown(void)
{
  stop_grabbing_messages();
  deinit_libtest_filterx();
  cfg_free(configuration);
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(filterx_eval, .init = setup, .fini = teardown);

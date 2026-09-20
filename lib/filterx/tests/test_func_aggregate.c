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
#include "libtest/mock-logpipe.h"

#include "filterx/func-aggregate.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-pipe.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-compound.h"
#include "filterx/expr-function.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-sequence.h"
#include "filterx/expr-variable.h"
#include "filterx/expr-assign.h"
#include "filterx/filterx-scope.h"

#include "apphook.h"
#include "scratch-buffers.h"
#include "logpipe.h"
#include "logmsg/logmsg.h"
#include "cfg.h"

/* a values_expr stand-in that counts how many times it actually got
 * evaluated, to prove replay skips it entirely (see
 * test_replay_does_not_reevaluate_arguments below). */
typedef struct
{
  FilterXExpr super;
  gint *eval_count;
} CountingValuesExpr;

static FilterXObject *
_counting_values_expr_eval(FilterXExpr *s)
{
  CountingValuesExpr *self = (CountingValuesExpr *) s;
  (*self->eval_count)++;

  FilterXObject *values_dict = filterx_dict_new();
  FILTERX_STRING_DECLARE_ON_STACK(count_key, "count", -1);
  FilterXObject *count_value = filterx_integer_new(1);
  cr_assert(filterx_object_set_subscript(values_dict, count_key, &count_value));
  filterx_object_unref(count_value);
  FILTERX_STRING_CLEAR_FROM_STACK(count_key);
  return values_dict;
}

static gboolean
_counting_values_expr_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  return TRUE;
}

static FilterXExpr *
_counting_values_expr_new(gint *eval_count)
{
  CountingValuesExpr *self = g_new0(CountingValuesExpr, 1);

  filterx_expr_init_instance(&self->super, "test-counting-values", FXE_READ);
  self->super.eval = _counting_values_expr_eval;
  self->super.walk_children = _counting_values_expr_walk;
  self->eval_count = eval_count;
  return &self->super;
}

static FilterXExpr *
_new_aggregate_with_counting_values(const gchar *key, gint64 timeout, gint *values_eval_count)
{
  GList *args = NULL;

  args = g_list_append(args, filterx_function_arg_new("key", filterx_literal_new(filterx_string_new(key, -1))));
  args = g_list_append(args, filterx_function_arg_new("values", _counting_values_expr_new(values_eval_count)));
  args = g_list_append(args, filterx_function_arg_new("timeout", filterx_literal_new(filterx_integer_new(timeout))));

  GError *args_err = NULL;
  GError *err = NULL;
  FilterXExpr *agg = filterx_function_aggregate_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert_null(args_err);
  cr_assert_null(err);
  cr_assert_not_null(agg);
  return agg;
}

static FilterXExpr *
_new_aggregate(const gchar *key, gint64 count, gint64 timeout)
{
  GList *args = NULL;

  args = g_list_append(args, filterx_function_arg_new("key", filterx_literal_new(filterx_string_new(key, -1))));

  FilterXObject *values_dict = filterx_dict_new();
  FILTERX_STRING_DECLARE_ON_STACK(count_key, "count", -1);
  FilterXObject *count_value = filterx_integer_new(count);
  cr_assert(filterx_object_set_subscript(values_dict, count_key, &count_value));
  filterx_object_unref(count_value);
  FILTERX_STRING_CLEAR_FROM_STACK(count_key);
  args = g_list_append(args, filterx_function_arg_new("values", filterx_literal_new(values_dict)));

  args = g_list_append(args, filterx_function_arg_new("timeout", filterx_literal_new(filterx_integer_new(timeout))));

  GError *args_err = NULL;
  GError *err = NULL;
  FilterXExpr *agg = filterx_function_aggregate_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert_null(args_err);
  cr_assert_null(err);
  cr_assert_not_null(agg);
  return agg;
}

static FilterXObject *
_new_field_aggregators_dict(const gchar *explicit_sum_func, const gchar *replaced_func)
{
  FilterXObject *dict = filterx_dict_new();

  FILTERX_STRING_DECLARE_ON_STACK(explicit_sum_key, "explicit_sum", -1);
  FilterXObject *explicit_sum_value = filterx_string_new(explicit_sum_func, -1);
  cr_assert(filterx_object_set_subscript(dict, explicit_sum_key, &explicit_sum_value));
  filterx_object_unref(explicit_sum_value);
  FILTERX_STRING_CLEAR_FROM_STACK(explicit_sum_key);

  FILTERX_STRING_DECLARE_ON_STACK(replaced_key, "replaced", -1);
  FilterXObject *replaced_value = filterx_string_new(replaced_func, -1);
  cr_assert(filterx_object_set_subscript(dict, replaced_key, &replaced_value));
  filterx_object_unref(replaced_value);
  FILTERX_STRING_CLEAR_FROM_STACK(replaced_key);

  return dict;
}

static FilterXExpr *
_new_aggregate_with_average_field(const gchar *key, gint64 count, gint64 timeout)
{
  GList *args = NULL;

  args = g_list_append(args, filterx_function_arg_new("key", filterx_literal_new(filterx_string_new(key, -1))));

  FilterXObject *values_dict = filterx_dict_new();
  FILTERX_STRING_DECLARE_ON_STACK(count_key, "count", -1);
  FilterXObject *count_value = filterx_integer_new(count);
  cr_assert(filterx_object_set_subscript(values_dict, count_key, &count_value));
  filterx_object_unref(count_value);
  args = g_list_append(args, filterx_function_arg_new("values", filterx_literal_new(values_dict)));

  FilterXObject *aggregators_dict = filterx_dict_new();
  FilterXObject *average_value = filterx_string_new("average", -1);
  cr_assert(filterx_object_set_subscript(aggregators_dict, count_key, &average_value));
  filterx_object_unref(average_value);
  FILTERX_STRING_CLEAR_FROM_STACK(count_key);
  args = g_list_append(args, filterx_function_arg_new("aggregators", filterx_literal_new(aggregators_dict)));

  args = g_list_append(args, filterx_function_arg_new("timeout", filterx_literal_new(filterx_integer_new(timeout))));

  GError *args_err = NULL;
  GError *err = NULL;
  FilterXExpr *agg = filterx_function_aggregate_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert_null(args_err);
  cr_assert_null(err);
  cr_assert_not_null(agg);
  return agg;
}

static FilterXExpr *
_new_aggregate_with_close_literal(const gchar *key, gint64 count, gint64 timeout, gboolean close)
{
  GList *args = NULL;

  args = g_list_append(args, filterx_function_arg_new("key", filterx_literal_new(filterx_string_new(key, -1))));

  FilterXObject *values_dict = filterx_dict_new();
  FILTERX_STRING_DECLARE_ON_STACK(count_key, "count", -1);
  FilterXObject *count_value = filterx_integer_new(count);
  cr_assert(filterx_object_set_subscript(values_dict, count_key, &count_value));
  filterx_object_unref(count_value);
  FILTERX_STRING_CLEAR_FROM_STACK(count_key);
  args = g_list_append(args, filterx_function_arg_new("values", filterx_literal_new(values_dict)));

  args = g_list_append(args, filterx_function_arg_new("timeout", filterx_literal_new(filterx_integer_new(timeout))));
  args = g_list_append(args, filterx_function_arg_new("close", filterx_literal_new(filterx_boolean_new(close))));

  GError *args_err = NULL;
  GError *err = NULL;
  FilterXExpr *agg = filterx_function_aggregate_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert_null(args_err);
  cr_assert_null(err);
  cr_assert_not_null(agg);
  return agg;
}

/* a real LogFilterXPipe, so continuation.owner_pipe is safe to use wherever
 * production code assumes that (e.g. log_filterx_pipe_resume_and_forward()).
 * Its own (trivial, unrelated) block doesn't matter for these tests. */
static LogPipe *
_new_owner_pipe(void)
{
  LogPipe *owner_pipe = log_filterx_pipe_new(filterx_literal_new(filterx_boolean_new(TRUE)), configuration);
  cr_assert(log_pipe_init(owner_pipe));
  return owner_pipe;
}

/* mimics what LogFilterXPipe's per-statement init driver
 * (filterx-pipe.c:_initialize_block_statements()) does: make a continuation
 * available on demand for agg's own init() via filterx_eval_get_continuation(). */
static void
_init_aggregate_as_sole_statement(FilterXExpr *agg, LogPipe *owner_pipe)
{
  FilterXEvalContext *ctx = filterx_eval_get_context();
  FilterXEvalContinuation continuation = { .owner_pipe = owner_pipe, .statement_expr = agg };

  ctx->continuation = &continuation;
  cr_assert(filterx_expr_init(agg, configuration));
  ctx->continuation = NULL;
}

/* mimics LogFilterXPipe's init when its block is a FilterXCompoundExpr:
 * only the *compound* is handed the top-level continuation; it is
 * expr-compound.c's init_child override that must narrow statement_expr
 * down to @agg (one of possibly several statements) by the time agg's own
 * init() runs, without owner_pipe getting lost along the way. */
static void
_init_compound_as_top_level_block(FilterXExpr *compound, LogPipe *owner_pipe)
{
  FilterXEvalContext *ctx = filterx_eval_get_context();
  FilterXEvalContinuation continuation = { .owner_pipe = owner_pipe, .statement_expr = compound };

  ctx->continuation = &continuation;
  cr_assert(filterx_expr_init(compound, configuration));
  ctx->continuation = NULL;
}

/* aggregate() returns a (status, values) tuple; these two just make the
 * tests below read naturally. */
static void
_assert_result_status(FilterXObject *result, const gchar *expected_status)
{
  FilterXObject *status_obj = filterx_sequence_get_subscript(result, 0);
  cr_assert_not_null(status_obj);
  cr_assert_str_eq(filterx_string_get_value_as_cstr(status_obj), expected_status);
  filterx_object_unref(status_obj);
}

static FilterXObject *
_result_values(FilterXObject *result)
{
  return filterx_sequence_get_subscript(result, 1);
}

/* end-to-end scenarios (message ordering, timer firing, per-field
 * aggregator selection, the "closed"/"timeout"/"absorbed" status
 * transitions as observed via syslog-ng) are covered by
 * tests/light/functional_tests/filterx/test_filterx_aggregate.py.  The
 * tests below stick to internals that are only observable from inside the
 * process: object aliasing, argument re-evaluation, and lifecycle safety. */

Test(func_aggregate, test_result_is_a_snapshot_unaffected_by_later_merges)
{
  FilterXExpr *agg = _new_aggregate("snapshotkey", 1, 60);

  LogPipeMock *sink = log_pipe_mock_new(configuration);
  cr_assert(log_pipe_init(&sink->super));
  LogPipe *owner_pipe = _new_owner_pipe();
  log_pipe_append(owner_pipe, &sink->super);

  _init_aggregate_as_sole_statement(agg, owner_pipe);

  /* message #1: snapshot the returned values dict, but keep holding onto it
   * (mimicking a caller still reading it after aggregate() returned,
   * possibly from another thread's message #2 evaluation) */
  FilterXObject *result1 = filterx_expr_eval(agg);
  cr_assert_not_null(result1);
  FilterXObject *values1 = _result_values(result1);

  FILTERX_STRING_DECLARE_ON_STACK(count_key, "count", -1);
  gint64 count1 = -1;
  cr_assert(filterx_object_extract_integer(filterx_object_get_subscript(values1, count_key), &count1));
  cr_assert_eq(count1, 1);

  /* message #2 for the same key: merges into the same group */
  FilterXObject *result2 = filterx_expr_eval(agg);
  cr_assert_not_null(result2);
  FilterXObject *values2 = _result_values(result2);
  gint64 count2 = -1;
  cr_assert(filterx_object_extract_integer(filterx_object_get_subscript(values2, count_key), &count2));
  cr_assert_eq(count2, 2);

  /* the whole point: values1, obtained before message #2 was merged, must
   * still read as it did at the time it was returned -- if aggregate()
   * handed out entry->values itself instead of a copy, this would now
   * read 2 as well */
  count1 = -1;
  cr_assert(filterx_object_extract_integer(filterx_object_get_subscript(values1, count_key), &count1));
  cr_assert_eq(count1, 1);

  FILTERX_STRING_CLEAR_FROM_STACK(count_key);
  filterx_object_unref(values1);
  filterx_object_unref(values2);
  filterx_object_unref(result1);
  filterx_object_unref(result2);

  filterx_expr_deinit(agg, configuration);
  filterx_expr_unref(agg);
  log_pipe_deinit(owner_pipe);
  log_pipe_deinit(&sink->super);
  log_pipe_unref(&sink->super);
  log_pipe_unref(owner_pipe);
}

Test(func_aggregate, test_deinit_drains_pending_entries_without_crashing)
{
  FilterXExpr *agg = _new_aggregate("otherkey", 1, 60);

  LogPipeMock *sink = log_pipe_mock_new(configuration);
  cr_assert(log_pipe_init(&sink->super));
  LogPipe *owner_pipe = _new_owner_pipe();
  log_pipe_append(owner_pipe, &sink->super);

  _init_aggregate_as_sole_statement(agg, owner_pipe);

  FilterXObject *result = filterx_expr_eval(agg);
  cr_assert_not_null(result);
  filterx_object_unref(result);

  /* deinit must safely unregister the still-pending timer, without ever
   * firing/replaying it */
  filterx_expr_deinit(agg, configuration);

  cr_assert_eq(sink->captured_messages->len, 0);

  filterx_expr_unref(agg);
  log_pipe_deinit(owner_pipe);
  log_pipe_deinit(&sink->super);
  log_pipe_unref(&sink->super);
  log_pipe_unref(owner_pipe);
}

/* "average" is the only aggregator that allocates its own per-(entry,
 * field) aux state (see FieldAverageState in func-aggregate.c); make sure
 * deinit()'s draining path frees it along with everything else, instead of
 * only exercising the plain sum path like the test above. */
Test(func_aggregate, test_deinit_drains_pending_entries_with_average_aux_state_without_leaking)
{
  FilterXExpr *agg = _new_aggregate_with_average_field("averagekey", 1, 60);

  LogPipeMock *sink = log_pipe_mock_new(configuration);
  cr_assert(log_pipe_init(&sink->super));
  LogPipe *owner_pipe = _new_owner_pipe();
  log_pipe_append(owner_pipe, &sink->super);

  _init_aggregate_as_sole_statement(agg, owner_pipe);

  FilterXObject *result = filterx_expr_eval(agg);
  cr_assert_not_null(result);
  filterx_object_unref(result);

  result = filterx_expr_eval(agg);
  cr_assert_not_null(result);
  filterx_object_unref(result);

  filterx_expr_deinit(agg, configuration);

  cr_assert_eq(sink->captured_messages->len, 0);

  filterx_expr_unref(agg);
  log_pipe_deinit(owner_pipe);
  log_pipe_deinit(&sink->super);
  log_pipe_unref(&sink->super);
  log_pipe_unref(owner_pipe);
}

Test(func_aggregate, test_expiring_an_already_closed_entry_is_a_no_op)
{
  FilterXExpr *agg = _new_aggregate("mykey", 1, 60);

  LogPipeMock *sink = log_pipe_mock_new(configuration);
  cr_assert(log_pipe_init(&sink->super));
  LogPipe *owner_pipe = _new_owner_pipe();
  log_pipe_append(owner_pipe, &sink->super);

  _init_aggregate_as_sole_statement(agg, owner_pipe);

  FilterXObject *result = filterx_expr_eval(agg);
  cr_assert_not_null(result);
  filterx_object_unref(result);

  /* the timer fires on the main thread with no filterx context active;
   * temporarily clear the libtest world's standing context to mimic that */
  FilterXEvalContext *standing_context = filterx_eval_get_context();
  filterx_eval_set_context(NULL);

  FILTERX_STRING_DECLARE_ON_STACK(fx_key, "mykey", -1);
  cr_assert(filterx_function_aggregate_test_expire(agg, fx_key));
  cr_assert_eq(sink->captured_messages->len, 1);

  /* the group is closed: expiring the same key again is a no-op, not a
   * second replay */
  cr_assert_not(filterx_function_aggregate_test_expire(agg, fx_key));
  cr_assert_eq(sink->captured_messages->len, 1);
  FILTERX_STRING_CLEAR_FROM_STACK(fx_key);

  filterx_eval_set_context(standing_context);

  filterx_expr_deinit(agg, configuration);
  filterx_expr_unref(agg);
  log_pipe_deinit(owner_pipe);
  log_pipe_deinit(&sink->super);
  log_pipe_unref(&sink->super);
  log_pipe_unref(owner_pipe);
}

Test(func_aggregate, test_replay_does_not_reevaluate_arguments)
{
  gint values_eval_count = 0;
  FilterXExpr *agg = _new_aggregate_with_counting_values("countkey", 60, &values_eval_count);

  LogPipeMock *sink = log_pipe_mock_new(configuration);
  cr_assert(log_pipe_init(&sink->super));
  LogPipe *owner_pipe = _new_owner_pipe();
  log_pipe_append(owner_pipe, &sink->super);

  _init_aggregate_as_sole_statement(agg, owner_pipe);

  FilterXObject *result = filterx_expr_eval(agg);
  cr_assert_not_null(result);
  filterx_object_unref(result);
  cr_assert_eq(values_eval_count, 1);

  result = filterx_expr_eval(agg);
  cr_assert_not_null(result);
  filterx_object_unref(result);
  cr_assert_eq(values_eval_count, 2);

  FilterXEvalContext *standing_context = filterx_eval_get_context();
  filterx_eval_set_context(NULL);

  FILTERX_STRING_DECLARE_ON_STACK(fx_key, "countkey", -1);
  cr_assert(filterx_function_aggregate_test_expire(agg, fx_key));
  FILTERX_STRING_CLEAR_FROM_STACK(fx_key);

  filterx_eval_set_context(standing_context);

  /* the whole point: replay hands back entry->values directly via the
   * resume value, without ever touching values_expr again */
  cr_assert_eq(values_eval_count, 2);
  cr_assert_eq(sink->captured_messages->len, 1);

  filterx_expr_deinit(agg, configuration);
  filterx_expr_unref(agg);
  log_pipe_deinit(owner_pipe);
  log_pipe_deinit(&sink->super);
  log_pipe_unref(&sink->super);
  log_pipe_unref(owner_pipe);
}

Test(func_aggregate, test_floating_variable_survives_timeout_replay)
{
  FilterXExpr *set_x = filterx_assign_new(filterx_floating_variable_expr_new("x"),
                                          filterx_literal_new(filterx_string_new("hello", -1)));
  FilterXExpr *agg = _new_aggregate("floatkey", 1, 60);
  FilterXExpr *set_msg = filterx_assign_new(filterx_msg_variable_expr_new("MSG"),
                                            filterx_floating_variable_expr_new("x"));
  FilterXExpr *compound = filterx_compound_expr_new_va(FALSE, set_x, agg, set_msg, NULL);

  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new(compound);
  set_libtest_filterx_scope(filterx_scope_new(NULL, layout));

  LogPipeMock *sink = log_pipe_mock_new(configuration);
  cr_assert(log_pipe_init(&sink->super));
  LogPipe *owner_pipe = _new_owner_pipe();
  log_pipe_append(owner_pipe, &sink->super);

  _init_compound_as_top_level_block(compound, owner_pipe);

  /* live path: sets x, runs aggregate() (arming the timer), then reads x
   * back via $MSG -- this direction always worked, since all statements of
   * a live pass share the same scope/generation */
  FilterXObject *result = filterx_expr_eval(compound);
  cr_assert_not_null(result);
  filterx_object_unref(result);

  cr_assert_eq(sink->captured_messages->len, 0);

  /* timeout replay: resumes evaluation starting at the aggregate()
   * statement, using the snapshot taken before it -- "x = ..." (before the
   * snapshot point) never re-runs, so this only works if the floating
   * variable's binding from the live pass survived into the snapshot and
   * is still visible to "$MSG = x;" (after the snapshot point) */
  FilterXEvalContext *standing_context = filterx_eval_get_context();
  filterx_eval_set_context(NULL);

  FILTERX_STRING_DECLARE_ON_STACK(fx_key, "floatkey", -1);
  cr_assert(filterx_function_aggregate_test_expire(agg, fx_key));
  FILTERX_STRING_CLEAR_FROM_STACK(fx_key);

  filterx_eval_set_context(standing_context);

  cr_assert_eq(sink->captured_messages->len, 1);
  LogMessage *forwarded = log_pipe_mock_get_message(sink, 0);
  gssize msg_len;
  const gchar *msg_value = log_msg_get_value(forwarded, LM_V_MESSAGE, &msg_len);
  cr_assert_eq(msg_len, 5);
  cr_assert_eq(memcmp(msg_value, "hello", 5), 0);

  filterx_expr_deinit(compound, configuration);
  filterx_expr_unref(compound);
  log_pipe_deinit(owner_pipe);
  log_pipe_deinit(&sink->super);
  log_pipe_unref(&sink->super);
  log_pipe_unref(owner_pipe);
  /* the scope itself is torn down by the libtest world's own teardown(),
   * but it does not own layout, so it must be freed here */
  filterx_scope_variable_layout_free(layout);
}

Test(func_aggregate, test_close_on_first_message_returns_value_without_arming_timer)
{
  FilterXExpr *agg = _new_aggregate_with_close_literal("closekey1", 1, 60, TRUE);

  LogPipeMock *sink = log_pipe_mock_new(configuration);
  cr_assert(log_pipe_init(&sink->super));
  LogPipe *owner_pipe = _new_owner_pipe();
  log_pipe_append(owner_pipe, &sink->super);

  _init_aggregate_as_sole_statement(agg, owner_pipe);

  /* closing on the very first message for a new key must still hand back
   * the merged value normally (live path), not via replay */
  FilterXObject *result = filterx_expr_eval(agg);
  cr_assert_not_null(result);
  _assert_result_status(result, "closed");
  filterx_object_unref(result);

  cr_assert_eq(sink->captured_messages->len, 0);

  /* the group was never armed/was immediately closed: no pending entry left */
  FilterXEvalContext *standing_context = filterx_eval_get_context();
  filterx_eval_set_context(NULL);

  FILTERX_STRING_DECLARE_ON_STACK(fx_key, "closekey1", -1);
  cr_assert_not(filterx_function_aggregate_test_expire(agg, fx_key));
  FILTERX_STRING_CLEAR_FROM_STACK(fx_key);

  filterx_eval_set_context(standing_context);

  filterx_expr_deinit(agg, configuration);
  filterx_expr_unref(agg);
  log_pipe_deinit(owner_pipe);
  log_pipe_deinit(&sink->super);
  log_pipe_unref(&sink->super);
  log_pipe_unref(owner_pipe);
}

/* the "aggregators" argument is only resolved to actual function pointers
 * at init() time (after the enclosing block has been optimized, so a dict
 * literal has had a chance to fold into a real literal expr -- see
 * _resolve_field_aggregators()), so construction itself always succeeds;
 * these three cases can only be observed to fail once the expr is
 * optimized and inited, same as e.g. regexp_search()'s pattern argument. */
static void
_assert_aggregators_init_error(FilterXExpr *aggregators_arg_expr)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new("key", filterx_literal_new(filterx_string_new("k", -1))));
  args = g_list_append(args, filterx_function_arg_new("values", filterx_literal_new(filterx_dict_new())));
  args = g_list_append(args, filterx_function_arg_new("timeout", filterx_literal_new(filterx_integer_new(60))));
  args = g_list_append(args, filterx_function_arg_new("aggregators", aggregators_arg_expr));

  GError *args_err = NULL;
  GError *err = NULL;
  FilterXExpr *agg = filterx_function_aggregate_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert_null(args_err);
  cr_assert_null(err);
  cr_assert_not_null(agg);

  agg = filterx_expr_optimize(agg);
  cr_assert_not(filterx_expr_init(agg, configuration));

  filterx_expr_unref(agg);
}

Test(func_aggregate, test_aggregators_argument_rejects_unknown_function_name)
{
  _assert_aggregators_init_error(filterx_literal_new(_new_field_aggregators_dict("no-such-function", "replace")));
}

Test(func_aggregate, test_aggregators_argument_must_be_a_literal_dict)
{
  /* not a literal at all */
  _assert_aggregators_init_error(filterx_floating_variable_expr_new("x"));
}

Test(func_aggregate, test_aggregators_argument_must_be_a_dict)
{
  /* literal, but not a dict */
  _assert_aggregators_init_error(filterx_literal_new(filterx_string_new("sum", -1)));
}

Test(func_aggregate, test_expr_is_compound_predicate)
{
  FilterXExpr *compound = filterx_compound_expr_new(FALSE);
  cr_assert(filterx_expr_is_compound(compound));

  FilterXExpr *literal = filterx_literal_new(filterx_integer_new(1));
  cr_assert_not(filterx_expr_is_compound(literal));

  filterx_expr_unref(compound);
  filterx_expr_unref(literal);
}

static void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
  cfg_init(configuration);
  init_libtest_filterx();
}

static void
teardown(void)
{
  deinit_libtest_filterx();
  cfg_free(configuration);
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(func_aggregate, .init = setup, .fini = teardown);

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

/*
 * This suite walks every allocation strategy a FilterXObject can have, and
 * checks what the three places that keep an object past its allocation do
 * with it:
 *
 *   - filterx_eval_retain_object() / filterx_eval_retain_dup() get an object
 *     out of the allocator of the current context,
 *   - a container store never keeps a message backed value,
 *   - filterx_eval_context_dup() takes a snapshot of a whole context, valid
 *     on any thread, for as long as it is kept.
 *
 * Wherever possible, independence is proven by tearing down the original
 * storage (freeing the allocator's areas or the LogMessage) and then
 * reading the kept value: under ASan a wrong decision shows up as a
 * use-after-free.
 *
 * The allocation strategies covered:
 *
 *   1) heap, reference counted            -- filterx_string_new() etc. with no allocator active
 *   2) thread-local pipeline allocator    -- same constructors with an active FilterXAllocator
 *   3) stack                              -- FILTERX_STRING_DECLARE_ON_STACK() & co (never retained directly)
 *   4) hibernated                         -- null, booleans, small integers, cached strings
 *   5) frozen                             -- literals, cache_json_file(): frozen into an environment
 *   6) message backed                     -- values borrowing their bytes from a LogMessage's NVTable
 *   7) mixed containers / indirect values -- heap objects that internally point at storage of another kind
 *   8) metrics_labels                     -- a container whose labels borrow bytes from the objects it keeps
 *   9) context snapshots                  -- filterx_eval_context_dup(), what it copies and what it shares
 */

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "filterx/filterx-eval.h"
#include "filterx/filterx-allocator.h"
#include "filterx/filterx-env.h"
#include "filterx/filterx-stashed-object.h"
#include "filterx/filterx-ref.h"
#include "filterx/filterx-scope.h"
#include "filterx/filterx-variable.h"
#include "filterx/filterx-mapping.h"
#include "filterx/filterx-sequence.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-tuple.h"
#include "filterx/object-message-value.h"
#include "filterx/object-metrics-labels.h"

#include <signal.h>

#include "logmsg/logmsg.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "cfg.h"

/*
 * The libtest world never uses the thread-local allocator (everything is
 * heap allocated), so these helpers plug one into the standing eval context
 * on demand.  _release_allocator_memory() is what makes allocator related
 * mistakes observable: it frees every area (filterx_allocator_empty() would
 * keep the first one outside debug builds), so any pointer still aimed into
 * the allocator is a real use-after-free afterwards, in every build.
 */
static FilterXAllocator test_allocator;
#define TEST_ALLOCATOR_ID 7

static void
_enable_allocator(void)
{
  if (!test_allocator.areas)
    filterx_allocator_init_ext(&test_allocator, TEST_ALLOCATOR_ID, 4096);
  filterx_eval_get_context()->allocator = &test_allocator;
}

static void
_disable_allocator(void)
{
  filterx_eval_get_context()->allocator = NULL;
}

/* The standing eval context holds a strong reference on every
 * weak-referenced object (see filterx_eval_store_weak_ref()) until it ends;
 * in production those die together with the allocator, at the end of the
 * context.  Here the allocator goes first, so let go of the allocator
 * resident ones beforehand, while their memory is still valid. */
static void
_drop_allocator_resident_weak_refs(void)
{
  GPtrArray *weak_refs = filterx_eval_get_context()->weak_refs;

  for (guint i = 0; i < weak_refs->len;)
    {
      FilterXObject *o = g_ptr_array_index(weak_refs, i);
      if (filterx_object_is_allocator_resident(o))
        g_ptr_array_remove_index_fast(weak_refs, i);
      else
        i++;
    }
}

static void
_release_allocator_memory(void)
{
  _disable_allocator();
  _drop_allocator_resident_weak_refs();
  filterx_allocator_clear(&test_allocator);
}

/* recursively assert that nothing reachable from @o still lives in the
 * allocator or borrows from a LogMessage */
static void _assert_decoupled(FilterXObject *o, const gchar *what);

static gboolean
_assert_decoupled_elem(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  const gchar *what = (const gchar *) user_data;
  _assert_decoupled(key, what);
  _assert_decoupled(value, what);
  return TRUE;
}

static void
_assert_decoupled(FilterXObject *o, const gchar *what)
{
  cr_assert_not_null(o, "%s: kept NULL", what);
  cr_assert_neq(o->allocator_id, TEST_ALLOCATOR_ID,
                "%s: a %s object still living in the test allocator is reachable from the kept value",
                what, filterx_object_get_type_name(o));
  cr_assert_not(o->is_nvtable_backed,
                "%s: a %s object still borrowing from the LogMessage is reachable from the kept value",
                what, filterx_object_get_type_name(o));

  if (filterx_object_is_ref(o))
    {
      _assert_decoupled(filterx_ref_unwrap_ro(o), what);
      return;
    }

  if (filterx_object_is_type(o, &FILTERX_TYPE_NAME(mapping)) ||
      filterx_object_is_type(o, &FILTERX_TYPE_NAME(sequence)))
    cr_assert(filterx_object_iter(o, _assert_decoupled_elem, (gpointer) what));
}

static void
_dict_set(FilterXObject *dict, const gchar *key, FilterXObject *value)
{
  FILTERX_STRING_DECLARE_ON_STACK(key_obj, key, -1);
  cr_assert(filterx_object_set_subscript(dict, key_obj, &value));
  filterx_object_unref(value);
  FILTERX_STRING_CLEAR_FROM_STACK(key_obj);
}

static void
_list_append(FilterXObject *list, FilterXObject *value)
{
  cr_assert(filterx_sequence_append(list, &value));
  filterx_object_unref(value);
}

#define NESTED_DICT_JSON "{\"name\":\"nested-value\",\"items\":[4242,\"list-element\"]}"

/* CoW-prepared (i.e.  wrapped in a FilterXRef) like every container a
 * filterx variable ends up holding.  4242 is outside the hibernated
 * small-integer cache. */
static FilterXObject *
_new_nested_dict(void)
{
  FilterXObject *dict = filterx_dict_new();
  FilterXObject *list = filterx_list_new();

  _dict_set(dict, "name", filterx_string_new("nested-value", -1));
  _list_append(list, filterx_integer_new(4242));
  _list_append(list, filterx_string_new("list-element", -1));
  _dict_set(dict, "items", list);

  filterx_object_cow_prepare(&dict);
  return dict;
}

/* allocate @ctor's result the way compile time does: in a restricted
 * context on top of @env, then freeze it into @env just like a literal is
 * frozen into the environment of the configuration. */
static FilterXObject *
_new_frozen_in_env(FilterXEnvironment *env, FilterXObject *(*ctor)(void))
{
  FilterXEvalContext compile_context;

  filterx_eval_begin_restricted_context(&compile_context, env);
  FilterXObject *o = ctor();
  filterx_env_freeze_object(env, &o);
  filterx_eval_end_restricted_context(&compile_context);

  cr_assert_eq(o->ref_cnt, FILTERX_OBJECT_REFCOUNT_FROZEN);
  return o;
}

static FilterXObject *
_new_frozen_string(void)
{
  return filterx_string_new("frozen-literal", -1);
}

/* 1) heap, reference counted: not in the allocator, so a plain reference is
 * all a retain takes */
Test(filterx_retain, heap_refcounted_object_is_shared)
{
  FilterXObject *o = filterx_string_new("heap-string", -1);
  cr_assert_not(filterx_object_is_allocator_resident(o));

  FilterXObject *r = filterx_object_ref(o);
  filterx_eval_retain_object(&r);
  cr_assert_eq(r, o, "heap object was needlessly duplicated");
  cr_assert_eq(o->ref_cnt, 2);
  filterx_object_unref(r);
  filterx_object_unref(o);
}

/* 2) thread-local allocator: freed en masse at the end of the eval context,
 * so a retain has to move the value out of it */
Test(filterx_retain, allocator_object_is_duplicated_onto_the_heap)
{
  _enable_allocator();
  FilterXObject *o = filterx_string_new("allocator-string", -1);
  cr_assert(filterx_object_is_allocator_resident(o));

  /* the retained copy lands wherever the caller points the allocator: off,
   * for a copy that has to outlive every arena */
  _disable_allocator();
  FilterXObject *r = filterx_object_ref(o);
  filterx_eval_retain_object(&r);
  cr_assert_neq(r, o, "allocator object was not duplicated");
  _assert_decoupled(r, "allocator object");

  filterx_object_unref(o);
  _release_allocator_memory();

  assert_object_str_equals(r, "allocator-string");
  filterx_object_unref(r);
}

/* 2b) same for a nested container: the duplicate must be deep, a shallow
 * copy would still hold allocator-allocated elements */
Test(filterx_retain, allocator_container_is_deep_copied)
{
  _enable_allocator();
  FilterXObject *o = _new_nested_dict();
  cr_assert(filterx_object_is_allocator_resident(o));
  cr_assert(filterx_object_is_ref(o));

  _disable_allocator();
  FilterXObject *r = filterx_object_ref(o);
  filterx_eval_retain_object(&r);
  cr_assert_neq(r, o);
  cr_assert_not(r->floating_ref, "a retained container must be a grounded xref");
  _assert_decoupled(r, "allocator container");

  filterx_object_unref(o);
  _release_allocator_memory();

  assert_object_json_equals(r, NESTED_DICT_JSON);
  filterx_object_unref(r);
}

/* 2c) a floating xref from the allocator (what a function returns before
 * its result is stored anywhere) must come back as a grounded, heap xref */
Test(filterx_retain, allocator_floating_ref_is_duplicated_and_grounded)
{
  _enable_allocator();
  FilterXObject *o = filterx_ref_float(_new_nested_dict());
  cr_assert(o->floating_ref);

  _disable_allocator();
  FilterXObject *r = filterx_object_ref(o);
  filterx_eval_retain_object(&r);
  cr_assert_neq(r, o);
  cr_assert_not(r->floating_ref);
  _assert_decoupled(r, "floating");

  filterx_object_unref(o);
  _release_allocator_memory();
  assert_object_json_equals(r, NESTED_DICT_JSON);
  filterx_object_unref(r);
}

/* 3) stack: not reference counted at all, and never what a caller means to
 * retain -- every production caller hands retain an owned reference, and
 * filterx_object_ref() already clones a stack object on the way.  Retaining
 * one directly is a caller bug, caught in debug builds. */
#if SYSLOG_NG_ENABLE_DEBUG
Test(filterx_retain, retaining_a_stack_object_is_a_contract_violation, .signal = SIGABRT)
{
  FILTERX_STRING_DECLARE_ON_STACK(s, "stack-string", -1);
  cr_assert_eq(s->ref_cnt, FILTERX_OBJECT_REFCOUNT_STACK);

  filterx_eval_retain_dup(s);
}
#endif

/* 3b) what a caller does instead: the reference it takes on a stack object
 * is a clone in the active allocator, and that clone retains like any other
 * allocator object */
Test(filterx_retain, referenced_stack_object_retains_as_an_allocator_object)
{
  _enable_allocator();
  FILTERX_STRING_DECLARE_ON_STACK(s, "stack-string", -1);
  FilterXObject *o = filterx_object_ref(s);
  cr_assert_neq(o, s);
  cr_assert_eq(o->allocator_id, TEST_ALLOCATOR_ID);
  FILTERX_STRING_CLEAR_FROM_STACK(s);

  _disable_allocator();
  FilterXObject *r = filterx_object_ref(o);
  filterx_eval_retain_object(&r);
  cr_assert_neq(r, o);
  _assert_decoupled(r, "stack via ref");

  filterx_object_unref(o);
  _release_allocator_memory();
  assert_object_str_equals(r, "stack-string");
  filterx_object_unref(r);
}

/* 3c) the copy a retain makes lands in the allocator in effect at the call:
 * the caller chooses the destination (an arena of its own, or the heap by
 * switching the allocator off), the object layer does not second-guess it */
Test(filterx_retain, retained_copy_lands_in_the_allocator_in_effect)
{
  _enable_allocator();
  FilterXObject *o = filterx_string_new("allocator-string", -1);

  FilterXAllocator other_allocator = { 0 };
  filterx_allocator_init_ext(&other_allocator, TEST_ALLOCATOR_ID + 1, 4096);
  filterx_eval_get_context()->allocator = &other_allocator;

  FilterXObject *r = filterx_eval_retain_dup(o);
  cr_assert_eq(r->allocator_id, TEST_ALLOCATOR_ID + 1, "the copy did not land in the active allocator");
  filterx_object_unref(r);
  filterx_allocator_clear(&other_allocator);

  _disable_allocator();
  r = filterx_eval_retain_dup(o);
  cr_assert_not(filterx_object_is_allocator_resident(r), "with the allocator off, the copy must be a heap object");
  filterx_object_unref(r);

  filterx_object_unref(o);
  _release_allocator_memory();
}

Test(filterx_retain, current_allocator_id_follows_the_heap_fallback_of_an_oversized_allocation)
{
  _enable_allocator();
  cr_assert_eq(filterx_eval_current_allocator_id(sizeof(FilterXString)), TEST_ALLOCATOR_ID);
  cr_assert_eq(filterx_eval_current_allocator_id(FILTERX_ALLOCATOR_MAX_ALLOC_SIZE + 1), FILTERX_ALLOCATOR_ID_HEAP);

  _disable_allocator();
  cr_assert_eq(filterx_eval_current_allocator_id(sizeof(FilterXString)), FILTERX_ALLOCATOR_ID_HEAP);
}

/* 4) hibernated: process lifetime singletons, nothing to do */
Test(filterx_retain, hibernated_object_is_returned_as_is)
{
  FilterXObject *hibernated[] =
  {
    filterx_null_new(),
    filterx_boolean_new(TRUE),
    filterx_integer_new(5),
    filterx_string_new("", -1),
  };

  for (gsize h = 0; h < G_N_ELEMENTS(hibernated); h++)
    {
      FilterXObject *o = hibernated[h];
      cr_assert_eq(o->ref_cnt, FILTERX_OBJECT_REFCOUNT_HIBERNATED);

      FilterXObject *r = filterx_object_ref(o);
      filterx_eval_retain_object(&r);
      cr_assert_eq(r, o, "%s", filterx_object_get_type_name(o));
      filterx_object_unref(r);
      filterx_object_unref(o);
    }
}

/* 5) frozen (a literal, a cache_json_file() tree): lives as long as its
 * environment does, and that outlives the evaluation, so a reference is all
 * a retain takes. */
Test(filterx_retain, frozen_object_is_shared)
{
  FilterXEnvironment env;
  filterx_env_init(&env);
  FilterXObject *o = _new_frozen_in_env(&env, _new_frozen_string);

  FilterXObject *r = filterx_eval_retain_dup(o);
  cr_assert_eq(r, o);
  filterx_object_unref(r);

  filterx_env_clear(&env);
}

/* 6) message backed: the bytes of the object live in the LogMessage's
 * NVTable, and a write to the message can move or overwrite them.  A
 * variable may hold such a value, a container never does: the store keeps
 * an owned copy. */
static LogMessage *
_new_message_with_values(void)
{
  LogMessage *msg = log_msg_new_empty();
  log_msg_set_value_by_name_with_type(msg, "str", "nvtable-string-value", -1, LM_VT_STRING);
  log_msg_set_value_by_name_with_type(msg, "int", "12345", -1, LM_VT_INTEGER);
  return msg;
}

Test(filterx_retain, message_backed_string_is_copied_when_a_dict_stores_it)
{
  LogMessage *msg = _new_message_with_values();
  FilterXObject *value = filterx_extract_object_from_logmsg(msg, log_msg_get_value_handle("str"));
  cr_assert(filterx_object_is_type(value, &FILTERX_TYPE_NAME(string)));
  cr_assert(value->is_nvtable_backed);

  FilterXObject *dict = filterx_dict_new();
  _dict_set(dict, "field", filterx_object_ref(value));
  _assert_decoupled(dict, "dict value");

  filterx_object_unref(value);
  log_msg_unref(msg);
  assert_object_json_equals(dict, "{\"field\":\"nvtable-string-value\"}");
  filterx_object_unref(dict);
}

Test(filterx_retain, message_backed_message_value_is_copied_when_a_list_stores_it)
{
  LogMessage *msg = _new_message_with_values();
  FilterXObject *value = filterx_extract_object_from_logmsg(msg, log_msg_get_value_handle("int"));
  cr_assert(filterx_object_is_type(value, &FILTERX_TYPE_NAME(message_value)));
  cr_assert(value->is_nvtable_backed);

  FilterXObject *list = filterx_list_new();
  _list_append(list, value);
  _assert_decoupled(list, "list element");

  log_msg_unref(msg);
  assert_object_json_equals(list, "[12345]");
  filterx_object_unref(list);
}

Test(filterx_retain, message_backed_key_is_copied_when_a_dict_stores_it)
{
  LogMessage *msg = _new_message_with_values();
  FilterXObject *key = filterx_extract_object_from_logmsg(msg, log_msg_get_value_handle("str"));
  cr_assert(key->is_nvtable_backed);

  FilterXObject *dict = filterx_dict_new();
  FilterXObject *value = filterx_integer_new(4242);
  cr_assert(filterx_object_set_subscript(dict, key, &value));
  filterx_object_unref(value);
  _assert_decoupled(dict, "dict key");

  filterx_object_unref(key);
  log_msg_unref(msg);
  assert_object_json_equals(dict, "{\"nvtable-string-value\":4242}");
  filterx_object_unref(dict);
}

Test(filterx_retain, message_backed_string_is_copied_when_a_tuple_stores_it)
{
  LogMessage *msg = _new_message_with_values();
  FilterXObject *value = filterx_extract_object_from_logmsg(msg, log_msg_get_value_handle("str"));
  cr_assert(value->is_nvtable_backed);

  FilterXObject *tuple = filterx_tuple_new(1);
  filterx_tuple_set_subscript(tuple, 0, value);
  _assert_decoupled(tuple, "tuple element");

  filterx_object_unref(value);
  log_msg_unref(msg);
  assert_object_str_equals(filterx_tuple_peek_subscript(tuple, 0), "nvtable-string-value");
  filterx_object_unref(tuple);
}

Test(filterx_retain, value_stored_from_the_message_does_not_follow_a_write_to_the_message)
{
  LogMessage *msg = _new_message_with_values();
  FilterXObject *dict = filterx_dict_new();
  _dict_set(dict, "field", filterx_extract_object_from_logmsg(msg, log_msg_get_value_handle("str")));

  /* same length as the original, so the NVTable entry is reused */
  log_msg_set_value_by_name_with_type(msg, "str", "OVERWRITTEN-IN-PLACE", -1, LM_VT_STRING);

  assert_object_json_equals(dict, "{\"field\":\"nvtable-string-value\"}");
  filterx_object_unref(dict);
  log_msg_unref(msg);
}

/* 7) mixed storage.  A retain looks at the id of the object it is handed,
 * so a heap object whose internals point at arena memory would be retained
 * by reference and dangle.  That is why storing such a child is a contract
 * violation, caught at the store site in debug builds: whoever builds a
 * heap container during an evaluation has to switch the allocator off (or
 * copy the child) first. */

#if SYSLOG_NG_ENABLE_DEBUG
/* 7a) a heap container (allocated with the allocator switched off) being
 * handed an allocator-allocated element */
Test(filterx_retain, storing_an_allocator_element_into_a_heap_container_is_a_contract_violation, .signal = SIGABRT)
{
  _disable_allocator();
  FilterXObject *dict = filterx_dict_new();
  cr_assert_not(filterx_object_is_allocator_resident(dict));

  _enable_allocator();
  _dict_set(dict, "elem", filterx_string_new("from-the-allocator", -1));
}

/* 7a') a heap metrics_labels being handed an allocator-allocated value:
 * it keeps the key/value objects, so it is a container in this respect too */
Test(filterx_retain, storing_an_allocator_value_into_heap_metrics_labels_is_a_contract_violation, .signal = SIGABRT)
{
  _disable_allocator();
  FilterXObject *ml = filterx_object_metrics_labels_new(1);
  filterx_object_cow_prepare(&ml);

  _enable_allocator();
  FILTERX_STRING_DECLARE_ON_STACK(key, "label-name", -1);
  FilterXObject *value = filterx_string_new("label-value", -1);
  cr_assert(filterx_object_set_subscript(ml, key, &value));
}
#endif

/* 7b) a heap string slice taken over an allocator-allocated base string:
 * a slice normally borrows the base's bytes (FilterXString's STR_INDIRECT
 * storage), which here would mean a heap object pointing into the
 * allocator.  The slice copies the bytes up front, so the retained value
 * does not depend on the base. */
Test(filterx_retain, heap_slice_of_an_allocator_string_does_not_borrow_from_it)
{
  _enable_allocator();
  FilterXObject *base = filterx_string_new("a-fairly-long-base-string-value", -1);
  cr_assert(filterx_object_is_allocator_resident(base));

  _disable_allocator();
  /* a slice under the limit is a fresh string, which borrows nothing */
  G_STATIC_ASSERT(22 - 2 >= FILTERX_STRING_SHORT_SLICE_LIMIT);
  FilterXObject *slice = filterx_string_new_slice(base, 2, 22);
  cr_assert_not(filterx_object_is_allocator_resident(slice));
  assert_object_str_equals(slice, "fairly-long-base-str");

  FilterXObject *r = filterx_object_ref(slice);
  filterx_eval_retain_object(&r);
  _assert_decoupled(r, "slice");

  filterx_object_unref(slice);
  filterx_object_unref(base);
  _release_allocator_memory();
  assert_object_str_equals(r, "fairly-long-base-str");
  filterx_object_unref(r);
}

/* 8) metrics_labels: a container whose labels borrow their bytes from the
 * key/value objects it keeps, so it is only as independent as its clone is */
static FilterXObject *
_new_metrics_labels(FilterXObject *value)
{
  FilterXObject *ml = filterx_object_metrics_labels_new(1);
  FILTERX_STRING_DECLARE_ON_STACK(key, "label-name", -1);
  cr_assert(filterx_object_set_subscript(ml, key, &value));
  filterx_object_unref(value);
  FILTERX_STRING_CLEAR_FROM_STACK(key);
  filterx_object_cow_prepare(&ml);
  return ml;
}

static void
_assert_repr_matches(FilterXObject *actual, FilterXObject *expected)
{
  GString *ra = g_string_new(NULL), *re = g_string_new(NULL);
  cr_assert(filterx_object_repr(actual, ra));
  cr_assert(filterx_object_repr(expected, re));
  cr_assert_str_eq(ra->str, re->str);
  g_string_free(ra, TRUE);
  g_string_free(re, TRUE);
}

Test(filterx_retain, allocator_metrics_labels_is_deep_copied)
{
  _disable_allocator();
  FilterXObject *expected = _new_metrics_labels(filterx_string_new("label-value", -1));

  _enable_allocator();
  FilterXObject *o = _new_metrics_labels(filterx_string_new("label-value", -1));
  cr_assert(filterx_object_is_allocator_resident(o));

  _disable_allocator();
  FilterXObject *r = filterx_object_ref(o);
  filterx_eval_retain_object(&r);
  cr_assert_neq(r, o);
  _assert_decoupled(r, "metrics_labels");

  filterx_object_unref(o);
  _release_allocator_memory();
  _assert_repr_matches(r, expected);
  filterx_object_unref(r);
  filterx_object_unref(expected);
}

Test(filterx_retain, metrics_labels_keeps_a_copy_of_a_message_backed_value)
{
  FilterXObject *expected = _new_metrics_labels(filterx_string_new("nvtable-string-value", -1));

  LogMessage *msg = _new_message_with_values();
  FilterXObject *ml = _new_metrics_labels(filterx_extract_object_from_logmsg(msg, log_msg_get_value_handle("str")));

  log_msg_unref(msg);
  _assert_repr_matches(ml, expected);
  filterx_object_unref(ml);
  filterx_object_unref(expected);
}

Test(filterx_retain, metrics_labels_keeps_a_copy_of_a_message_backed_key)
{
  FilterXObject *value = filterx_string_new("label-value", -1);

  FilterXObject *expected = filterx_object_metrics_labels_new(1);
  FILTERX_STRING_DECLARE_ON_STACK(expected_key, "nvtable-string-value", -1);
  cr_assert(filterx_object_set_subscript(expected, expected_key, &value));
  FILTERX_STRING_CLEAR_FROM_STACK(expected_key);

  LogMessage *msg = _new_message_with_values();
  FilterXObject *key = filterx_extract_object_from_logmsg(msg, log_msg_get_value_handle("str"));
  FilterXObject *ml = filterx_object_metrics_labels_new(1);
  cr_assert(filterx_object_set_subscript(ml, key, &value));
  filterx_object_unref(key);
  filterx_object_unref(value);

  log_msg_unref(msg);
  _assert_repr_matches(ml, expected);
  filterx_object_unref(ml);
  filterx_object_unref(expected);
}

/* 9) context snapshots: filterx_eval_context_dup() keeps its copies in an
 * arena of its own. */
Test(filterx_retain, snapshot_copies_allocator_allocated_variable_values)
{
  FilterXVariableHandle handle = filterx_map_varname_to_handle("arenavar", FX_VAR_DECLARED_FLOATING);
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(&handle, 1);
  FilterXScope *scope = filterx_scope_new(NULL, layout);
  set_libtest_filterx_scope(scope);

  _enable_allocator();
  FilterXVariable *v = filterx_scope_register_variable(scope, FX_VAR_DECLARED_FLOATING, handle, 0);
  FilterXObject *value = _new_nested_dict();
  cr_assert(filterx_object_is_allocator_resident(value));
  filterx_scope_set_variable(scope, v, &value, TRUE);
  filterx_object_unref(value);

  FilterXEvalContext *dup = filterx_eval_context_dup(filterx_eval_get_context());
  cr_assert_not_null(dup);

  /* drop the live scope (and with it the allocator-resident original) while
   * the allocator's memory is still valid, then pull the rug: only the
   * duplicate's copies remain reachable */
  set_libtest_filterx_scope(filterx_scope_new(NULL, NULL));
  _release_allocator_memory();

  FilterXVariable *dv = filterx_scope_lookup_variable(dup->scope, handle, 0);
  cr_assert_not_null(dv);
  FilterXObject *dvalue = filterx_variable_borrow_value(dv);
  _assert_decoupled(dvalue, "context_dup");
  assert_object_json_equals(dvalue, NESTED_DICT_JSON);

  /* the copies live in the snapshot's own arena ... */
  cr_assert_eq(dvalue->allocator_id, FILTERX_ALLOCATOR_ID_PRIVATE);
  cr_assert_eq(filterx_ref_unwrap_ro(dvalue)->allocator_id, FILTERX_ALLOCATOR_ID_PRIVATE);

  /* ... and so does whatever a resumed evaluation allocates, except what
   * is too large for an arena, which falls back to the heap as always */
  FilterXEvalContext *standing_context = filterx_eval_get_context();
  filterx_eval_set_context(dup);
  FilterXObject *small = filterx_string_new("made-during-replay", -1);
  cr_assert_eq(small->allocator_id, FILTERX_ALLOCATOR_ID_PRIVATE);
  gchar *big_str = g_strnfill(2 * FILTERX_ALLOCATOR_MAX_ALLOC_SIZE, 'x');
  FilterXObject *big = filterx_string_new(big_str, -1);
  cr_assert_not(filterx_object_is_allocator_resident(big));
  filterx_object_unref(small);
  filterx_object_unref(big);
  g_free(big_str);
  filterx_eval_set_context(standing_context);

  filterx_eval_context_free_dup(dup);
  filterx_scope_variable_layout_free(layout);
}

/* a resumed evaluation writes through the snapshot, so a heap container is
 * copied.  An immutable heap object is shared: the reference count of a
 * heap object is atomic */
Test(filterx_retain, snapshot_copies_a_heap_container_and_shares_an_immutable_heap_object)
{
  FilterXVariableHandle handles[] =
  {
    filterx_map_varname_to_handle("heapdict", FX_VAR_DECLARED_FLOATING),
    filterx_map_varname_to_handle("heapstr", FX_VAR_DECLARED_FLOATING),
  };
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));
  FilterXScope *scope = filterx_scope_new(NULL, layout);
  set_libtest_filterx_scope(scope);

  FilterXVariable *v = filterx_scope_register_variable(scope, FX_VAR_DECLARED_FLOATING, handles[0], 0);
  FilterXObject *dict = _new_nested_dict();
  cr_assert_not(filterx_object_is_allocator_resident(dict));
  filterx_scope_set_variable(scope, v, &dict, TRUE);

  v = filterx_scope_register_variable(scope, FX_VAR_DECLARED_FLOATING, handles[1], 1);
  FilterXObject *str = filterx_string_new("heap-string", -1);
  filterx_scope_set_variable(scope, v, &str, TRUE);

  guint32 str_refs = str->ref_cnt;
  FilterXEvalContext *dup = filterx_eval_context_dup(filterx_eval_get_context());

  FilterXObject *ddict = filterx_variable_borrow_value(filterx_scope_lookup_variable(dup->scope, handles[0], 0));
  cr_assert_neq(ddict, dict, "the snapshot shares a heap xref with the live evaluation");
  cr_assert_neq(filterx_ref_unwrap_ro(ddict), filterx_ref_unwrap_ro(dict),
                "the snapshot shares a heap container with the live evaluation");
  assert_object_json_equals(ddict, NESTED_DICT_JSON);

  FilterXObject *dstr = filterx_variable_borrow_value(filterx_scope_lookup_variable(dup->scope, handles[1], 1));
  cr_assert_eq(dstr, str, "the snapshot copied an immutable heap object");
  cr_assert_eq(str->ref_cnt, str_refs + 1, "the snapshot holds no reference of its own");

  filterx_object_unref(dict);
  filterx_object_unref(str);
  filterx_eval_context_free_dup(dup);
  filterx_scope_variable_layout_free(layout);
}

Test(filterx_retain, snapshot_copies_a_message_backed_variable_value)
{
  LogMessage *msg = filterx_eval_get_context()->msg;
  log_msg_set_value_by_name_with_type(msg, "snapsrc", "nvtable-string-value", -1, LM_VT_STRING);

  FilterXVariableHandle handle = filterx_map_varname_to_handle("msgvar", FX_VAR_DECLARED_FLOATING);
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(&handle, 1);
  FilterXScope *scope = filterx_scope_new(NULL, layout);
  set_libtest_filterx_scope(scope);

  FilterXVariable *v = filterx_scope_register_variable(scope, FX_VAR_DECLARED_FLOATING, handle, 0);
  FilterXObject *value = filterx_extract_object_from_logmsg(msg, log_msg_get_value_handle("snapsrc"));
  filterx_scope_set_variable(scope, v, &value, TRUE);
  filterx_object_unref(value);
  cr_assert(filterx_variable_borrow_value(v)->is_nvtable_backed, "a variable keeps a message backed value as it is");

  FilterXEvalContext *dup = filterx_eval_context_dup(filterx_eval_get_context());

  FilterXObject *dvalue = filterx_variable_borrow_value(filterx_scope_lookup_variable(dup->scope, handle, 0));
  _assert_decoupled(dvalue, "snapshot");
  assert_object_str_equals(dvalue, "nvtable-string-value");

  filterx_eval_context_free_dup(dup);
  filterx_scope_variable_layout_free(layout);
}

/* A frozen object is not copied: the snapshot takes its own references to
 * the stashes of the evaluation. */
static void
_stash_store_nested_dict(FilterXStashedObject **stash)
{
  FilterXEnvironment env;
  FilterXEvalContext build_context;

  filterx_env_init(&env);
  filterx_eval_begin_restricted_context(&build_context, &env);
  FilterXObject *tree = _new_nested_dict();
  filterx_eval_end_restricted_context(&build_context);

  cr_assert(filterx_stash_store(stash, tree, &env));
  filterx_env_clear(&env);
}

Test(filterx_retain, snapshot_pins_stashed_objects_instead_of_copying_them)
{
  FilterXStashedObject *stash = NULL;
  _stash_store_nested_dict(&stash);

  FilterXVariableHandle tree_handle = filterx_map_varname_to_handle("stashtree", FX_VAR_DECLARED_FLOATING);
  FilterXVariableHandle items_handle = filterx_map_varname_to_handle("stashitems", FX_VAR_DECLARED_FLOATING);
  FilterXVariableHandle handles[] = { tree_handle, items_handle };
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));
  FilterXScope *scope = filterx_scope_new(NULL, layout);
  set_libtest_filterx_scope(scope);

  _enable_allocator();
  FilterXObject *tree = filterx_stash_retrieve(&stash);
  cr_assert_eq(tree->ref_cnt, FILTERX_OBJECT_REFCOUNT_FROZEN);
  FilterXObject *tree_value = filterx_ref_unwrap_ro(tree);

  FilterXVariable *v = filterx_scope_register_variable(scope, FX_VAR_DECLARED_FLOATING, tree_handle, 0);
  filterx_scope_set_variable(scope, v, &tree, TRUE);
  filterx_object_unref(tree);

  /* and a sub-object pulled out of it, the way lookup["items"] would */
  FILTERX_STRING_DECLARE_ON_STACK(items_key, "items", -1);
  FilterXObject *items = filterx_object_get_subscript(tree, items_key);
  FILTERX_STRING_CLEAR_FROM_STACK(items_key);
  cr_assert_not_null(items);
  FilterXObject *items_value = filterx_ref_unwrap_ro(items);
  cr_assert_eq(items_value->ref_cnt, FILTERX_OBJECT_REFCOUNT_FROZEN);

  v = filterx_scope_register_variable(scope, FX_VAR_DECLARED_FLOATING, items_handle, 1);
  filterx_scope_set_variable(scope, v, &items, TRUE);
  filterx_object_unref(items);

  FilterXEvalContext *dup = filterx_eval_context_dup(filterx_eval_get_context());

  FilterXObject *dtree = filterx_variable_borrow_value(filterx_scope_lookup_variable(dup->scope, tree_handle, 0));
  FilterXObject *ditems = filterx_variable_borrow_value(filterx_scope_lookup_variable(dup->scope, items_handle, 1));
  cr_assert_eq(filterx_ref_unwrap_ro(dtree), tree_value, "the stashed tree was copied into the snapshot");
  cr_assert_eq(filterx_ref_unwrap_ro(ditems), items_value,
               "a sub-object of the stashed tree was copied into the snapshot");
  _assert_decoupled(ditems, "snapshot");

  /* the file gets reloaded (a new version replaces the old in the stash)
   * and the live evaluation ends (its scope, allocator and weak refs go):
   * from here on only the snapshot keeps the old version alive */
  _stash_store_nested_dict(&stash);
  set_libtest_filterx_scope(filterx_scope_new(NULL, NULL));
  _release_allocator_memory();
  g_ptr_array_set_size(filterx_eval_get_context()->weak_refs, 0);

  assert_object_json_equals(dtree, NESTED_DICT_JSON);
  assert_object_json_equals(ditems, "[4242,\"list-element\"]");

  /* releasing the snapshot releases the old version */
  filterx_eval_context_free_dup(dup);
  filterx_scope_variable_layout_free(layout);
  filterx_stashed_object_unref(stash);
}

/* The evaluation takes no reference to the stash of its configuration, the
 * snapshot takes one. */
Test(filterx_retain, snapshot_takes_a_reference_to_the_stash_of_the_configuration)
{
  FilterXStashedObject *config_stash = filterx_stashed_env_new();
  LogMessage *msg = log_msg_new_empty();
  FilterXEvalContext *standing_context = filterx_eval_get_context();
  FilterXEvalContext eval_context;
  FilterXEvalContext *dup = NULL;

  FILTERX_EVAL_BEGIN_CONTEXT(eval_context, NULL, &config_stash, msg, NULL)
  {
    cr_assert_eq(g_atomic_counter_get(&config_stash->ref_cnt), 1);

    dup = filterx_eval_context_dup(&eval_context);
    cr_assert_eq(g_atomic_counter_get(&config_stash->ref_cnt), 2);
  }
  FILTERX_EVAL_END_CONTEXT(eval_context);
  filterx_eval_set_context(standing_context);

  cr_assert_eq(g_atomic_counter_get(&config_stash->ref_cnt), 2);
  filterx_eval_context_free_dup(dup);
  cr_assert_eq(g_atomic_counter_get(&config_stash->ref_cnt), 1);

  log_msg_unref(msg);
  filterx_stashed_object_unref(config_stash);
}

typedef struct
{
  FilterXObject *literal;
  FilterXVariableHandle handle;
  FilterXScopeVariableLayout *layout;
  FilterXEvalContext *dup;
} ConfigLiteralSnapshot;

/* A snapshot of an evaluation that assigned a literal of its configuration
 * to a variable.  On return the evaluation is over and the configuration
 * has dropped its stash: only the snapshot keeps the literal alive. */
static void
_snapshot_a_config_literal(ConfigLiteralSnapshot *self)
{
  FilterXStashedObject *config_stash = filterx_stashed_env_new();
  self->literal = _new_frozen_in_env(&config_stash->env, _new_nested_dict);

  self->handle = filterx_map_varname_to_handle("literalvar", FX_VAR_DECLARED_FLOATING);
  self->layout = filterx_scope_variable_layout_new_from_handles(&self->handle, 1);
  FilterXScope *scope = filterx_scope_new(NULL, self->layout);
  set_libtest_filterx_scope(scope);

  /* the evaluation runs under this configuration */
  filterx_eval_get_context()->config_stash = &config_stash;
  _enable_allocator();

  /* an assignment leaves an xref of the evaluation around the frozen value */
  FilterXVariable *v = filterx_scope_register_variable(scope, FX_VAR_DECLARED_FLOATING, self->handle, 0);
  FilterXObject *value = filterx_object_cow_fork2(filterx_object_ref(self->literal), NULL);
  filterx_scope_set_variable(scope, v, &value, TRUE);
  filterx_object_unref(value);

  self->dup = filterx_eval_context_dup(filterx_eval_get_context());

  filterx_eval_get_context()->config_stash = NULL;
  set_libtest_filterx_scope(filterx_scope_new(NULL, NULL));
  _release_allocator_memory();
  g_ptr_array_set_size(filterx_eval_get_context()->weak_refs, 0);
  filterx_stashed_object_unref(config_stash);
}

static FilterXObject *
_snapshot_value(ConfigLiteralSnapshot *self)
{
  return filterx_variable_borrow_value(filterx_scope_lookup_variable(self->dup->scope, self->handle, 0));
}

static void
_free_snapshot(ConfigLiteralSnapshot *self)
{
  filterx_eval_context_free_dup(self->dup);
  filterx_scope_variable_layout_free(self->layout);
}

Test(filterx_retain, snapshot_shares_a_config_literal_and_keeps_it_alive)
{
  ConfigLiteralSnapshot snapshot;
  _snapshot_a_config_literal(&snapshot);

  FilterXObject *dvalue = _snapshot_value(&snapshot);
  cr_assert_eq(filterx_ref_unwrap_ro(dvalue), filterx_ref_unwrap_ro(snapshot.literal),
               "the config literal was copied into the snapshot");
  _assert_decoupled(dvalue, "snapshot");
  assert_object_json_equals(dvalue, NESTED_DICT_JSON);

  _free_snapshot(&snapshot);
}

Test(filterx_retain, a_write_through_the_snapshot_does_not_change_a_config_literal)
{
  ConfigLiteralSnapshot snapshot;
  _snapshot_a_config_literal(&snapshot);

  FilterXEvalContext *standing_context = filterx_eval_get_context();
  filterx_eval_set_context(snapshot.dup);
  _dict_set(_snapshot_value(&snapshot), "added", filterx_string_new("by-the-resumed-evaluation", -1));
  filterx_eval_set_context(standing_context);

  FilterXObject *dvalue = _snapshot_value(&snapshot);
  cr_assert_neq(filterx_ref_unwrap_ro(dvalue), filterx_ref_unwrap_ro(snapshot.literal),
                "the write went into the frozen literal");
  assert_object_json_equals(snapshot.literal, NESTED_DICT_JSON);
  assert_object_json_equals(dvalue, "{\"name\":\"nested-value\",\"items\":[4242,\"list-element\"],"
                                    "\"added\":\"by-the-resumed-evaluation\"}");

  _free_snapshot(&snapshot);
}

/* A resumed evaluation that reads into the literal leaves an xref around a
 * frozen object in the weak refs of the snapshot.  That xref touches the
 * object when it is released, so the environment has to be still there. */
Test(filterx_retain, snapshot_releases_its_stash_references_last)
{
  ConfigLiteralSnapshot snapshot;
  _snapshot_a_config_literal(&snapshot);

  FilterXEvalContext *standing_context = filterx_eval_get_context();
  filterx_eval_set_context(snapshot.dup);
  FilterXObject *items = filterx_object_getattr_string(_snapshot_value(&snapshot), "items");
  cr_assert_not_null(items);
  assert_object_json_equals(items, "[4242,\"list-element\"]");
  filterx_object_unref(items);
  filterx_eval_set_context(standing_context);

  _free_snapshot(&snapshot);
}

/* An error of a resumed evaluation can hold a frozen object too. */
Test(filterx_retain, snapshot_releases_its_stash_references_after_its_errors)
{
  ConfigLiteralSnapshot snapshot;
  _snapshot_a_config_literal(&snapshot);

  FilterXEvalContext *standing_context = filterx_eval_get_context();
  filterx_eval_set_context(snapshot.dup);
  filterx_eval_push_error("an error of the resumed evaluation", snapshot.literal);
  filterx_eval_set_context(standing_context);

  _free_snapshot(&snapshot);
}

static gboolean
_count_variable(FilterXVariable *variable, gpointer user_data)
{
  (*(gint *) user_data)++;
  return TRUE;
}

Test(filterx_retain, snapshot_syncs_a_whiteout_into_its_message)
{
  LogMessage *msg = filterx_eval_get_context()->msg;
  log_msg_set_value_by_name_with_type(msg, "snapunset", "to-be-unset", -1, LM_VT_STRING);

  FilterXVariableHandle handle = filterx_map_varname_to_handle("snapunset", FX_VAR_MESSAGE_TIED);
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(&handle, 1);
  FilterXScope *scope = filterx_scope_new(NULL, layout);
  set_libtest_filterx_scope(scope);

  FilterXVariable *v = filterx_scope_register_variable(scope, FX_VAR_MESSAGE_TIED, handle, 0);
  filterx_scope_unset_variable(scope, v);

  FilterXEvalContext *dup = filterx_eval_context_dup(filterx_eval_get_context());

  /* log_msg_is_value_set() stays TRUE for an unset entry, so look at the
   * value */
  NVHandle nv_handle = filterx_variable_get_nv_handle(v);
  LogMessageValueType type;
  cr_assert_not_null(log_msg_get_value_if_set_with_type(msg, nv_handle, NULL, &type));
  cr_assert_null(log_msg_get_value_if_set_with_type(dup->scope->msg, nv_handle, NULL, &type),
                 "the snapshot did not unset the field in its message");

  FilterXVariable *dv = filterx_scope_lookup_variable(dup->scope, handle, 0);
  cr_assert_not_null(dv);
  cr_assert_not(filterx_variable_is_assigned(dv), "the variable of the snapshot is not in sync with its message");

  filterx_eval_context_free_dup(dup);
  filterx_scope_variable_layout_free(layout);
}

Test(filterx_retain, snapshot_syncs_the_whiteout_of_an_earlier_block_into_its_message)
{
  LogMessage *msg = filterx_eval_get_context()->msg;
  log_msg_set_value_by_name_with_type(msg, "snapunset", "to-be-unset", -1, LM_VT_STRING);

  /* the earlier block unsets the field; filterx pipes chain their scopes,
   * so its scope is not synced before the next block runs */
  FilterXVariableHandle handle = filterx_map_varname_to_handle("snapunset", FX_VAR_MESSAGE_TIED);
  FilterXScopeVariableLayout *earlier_layout = filterx_scope_variable_layout_new_from_handles(&handle, 1);
  FilterXScope *earlier_scope = filterx_scope_new(NULL, earlier_layout);
  filterx_scope_set_message(earlier_scope, msg);
  FilterXVariable *v = filterx_scope_register_variable(earlier_scope, FX_VAR_MESSAGE_TIED, handle, 0);
  filterx_scope_unset_variable(earlier_scope, v);

  /* the current block never touches the variable */
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(NULL, 0);
  set_libtest_filterx_scope(filterx_scope_new(earlier_scope, layout));

  FilterXEvalContext *dup = filterx_eval_context_dup(filterx_eval_get_context());

  NVHandle nv_handle = filterx_variable_get_nv_handle(v);
  LogMessageValueType type;
  cr_assert_not_null(log_msg_get_value_if_set_with_type(msg, nv_handle, NULL, &type));
  cr_assert_null(log_msg_get_value_if_set_with_type(dup->scope->msg, nv_handle, NULL, &type),
                 "the snapshot did not unset the field of the earlier block in its message");

  filterx_eval_context_free_dup(dup);
  set_libtest_filterx_scope(filterx_scope_new(NULL, NULL));
  filterx_scope_free(earlier_scope);
  filterx_scope_variable_layout_free(layout);
  filterx_scope_variable_layout_free(earlier_layout);
}

Test(filterx_retain, snapshot_skips_message_tied_variables_that_are_in_sync_with_the_message)
{
  LogMessage *msg = filterx_eval_get_context()->msg;
  log_msg_set_value_by_name_with_type(msg, "snapread", "from-the-message", -1, LM_VT_STRING);
  log_msg_set_value_by_name_with_type(msg, "snapwrite", "to-be-overwritten", -1, LM_VT_STRING);

  FilterXVariableHandle read_handle = filterx_map_varname_to_handle("snapread", FX_VAR_MESSAGE_TIED);
  FilterXVariableHandle write_handle = filterx_map_varname_to_handle("snapwrite", FX_VAR_MESSAGE_TIED);
  FilterXVariableHandle handles[] = { read_handle, write_handle };
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));
  FilterXScope *scope = filterx_scope_new(NULL, layout);
  set_libtest_filterx_scope(scope);

  /* $snapread is only read: pulled into the scope, in sync with the message */
  FilterXVariable *v = filterx_scope_register_variable(scope, FX_VAR_MESSAGE_TIED, read_handle, 0);
  cr_assert_not(filterx_variable_is_assigned(v));
  assert_object_str_equals(filterx_variable_borrow_value(v), "from-the-message");

  /* $snapwrite is assigned to: only the scope knows the new value until the next sync */
  v = filterx_scope_register_variable(scope, FX_VAR_MESSAGE_TIED, write_handle, 1);
  FilterXObject *new_value = filterx_string_new("assigned-in-scope", -1);
  filterx_scope_set_variable(scope, v, &new_value, TRUE);
  filterx_object_unref(new_value);

  FilterXEvalContext *dup = filterx_eval_context_dup(filterx_eval_get_context());

  gint num_variables = 0;
  cr_assert(filterx_scope_foreach_variable_readonly(dup->scope, _count_variable, &num_variables));
  cr_assert_eq(num_variables, 1);

  FilterXVariable *dv = filterx_scope_lookup_variable(dup->scope, write_handle, 1);
  cr_assert_not_null(dv);
  assert_object_str_equals(filterx_variable_borrow_value(dv), "assigned-in-scope");

  cr_assert_not(filterx_variable_is_assigned(dv));
  cr_assert_str_eq(log_msg_get_value_by_name(dup->scope->msg, "snapwrite", NULL), "assigned-in-scope");
  cr_assert_str_eq(log_msg_get_value_by_name(msg, "snapwrite", NULL), "to-be-overwritten");

  cr_assert_null(filterx_scope_lookup_variable(dup->scope, read_handle, 0));
  dv = filterx_scope_register_variable(dup->scope, FX_VAR_MESSAGE_TIED, read_handle, 0);
  assert_object_str_equals(filterx_variable_borrow_value(dv), "from-the-message");

  filterx_eval_context_free_dup(dup);
  filterx_scope_variable_layout_free(layout);
}

Test(filterx_retain, snapshot_syncs_an_assignment_of_an_earlier_block_into_its_message)
{
  LogMessage *msg = filterx_eval_get_context()->msg;
  log_msg_set_value_by_name_with_type(msg, "snapearlier", "to-be-overwritten", -1, LM_VT_STRING);

  /* filterx pipes chain their scopes, so the scope of the earlier block is
   * not synced before the next block runs */
  FilterXVariableHandle handle = filterx_map_varname_to_handle("snapearlier", FX_VAR_MESSAGE_TIED);
  FilterXScopeVariableLayout *earlier_layout = filterx_scope_variable_layout_new_from_handles(&handle, 1);
  FilterXScope *earlier_scope = filterx_scope_new(NULL, earlier_layout);
  filterx_scope_set_message(earlier_scope, msg);
  FilterXVariable *v = filterx_scope_register_variable(earlier_scope, FX_VAR_MESSAGE_TIED, handle, 0);
  FilterXObject *new_value = filterx_string_new("assigned-in-an-earlier-block", -1);
  filterx_scope_set_variable(earlier_scope, v, &new_value, TRUE);
  filterx_object_unref(new_value);

  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new_from_handles(NULL, 0);
  set_libtest_filterx_scope(filterx_scope_new(earlier_scope, layout));

  FilterXEvalContext *dup = filterx_eval_context_dup(filterx_eval_get_context());

  cr_assert_str_eq(log_msg_get_value_by_name(dup->scope->msg, "snapearlier", NULL), "assigned-in-an-earlier-block");
  cr_assert_str_eq(log_msg_get_value_by_name(msg, "snapearlier", NULL), "to-be-overwritten");

  filterx_eval_context_free_dup(dup);
  set_libtest_filterx_scope(filterx_scope_new(NULL, NULL));
  filterx_scope_free(earlier_scope);
  filterx_scope_variable_layout_free(layout);
  filterx_scope_variable_layout_free(earlier_layout);
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
  _disable_allocator();
  /* the context goes first: its weak refs may still hold allocator objects */
  deinit_libtest_filterx();
  filterx_allocator_clear(&test_allocator);
  cfg_free(configuration);
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(filterx_retain, .init = setup, .fini = teardown);

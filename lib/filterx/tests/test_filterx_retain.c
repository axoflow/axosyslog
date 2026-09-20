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
 * checks what the two places that keep an object past its allocation do
 * with it:
 *
 *   - filterx_eval_retain_object() / filterx_eval_retain_dup() get an object
 *     out of the allocator of the current context,
 *   - a container store never keeps a message backed value.
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
 */

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "filterx/filterx-eval.h"
#include "filterx/filterx-allocator.h"
#include "filterx/filterx-env.h"
#include "filterx/filterx-ref.h"
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

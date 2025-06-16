/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "filterx/filterx-eval.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/json-repr.h"
#include "scratch-buffers.h"
#include "apphook.h"
#include "cfg.h"

FilterXEvalContext compile_context;


Test(filterx_cow, test_filterx_cow_wrap_adds_an_xref_wrapper)
{
  FilterXObject *d = filterx_dict_new();

  cr_assert(filterx_object_is_type(d, &FILTERX_TYPE_NAME(dict_object)));
  filterx_object_cow_prepare(&d);
  cr_assert(filterx_object_is_ref(d));

  filterx_object_unref(d);
}

static FilterXObject *
_attr_string(const gchar *attr)
{
  return filterx_string_new_frozen(attr, configuration);
}

static FilterXObject *
_value_string(const gchar *attr)
{
  return filterx_string_new_frozen(attr, configuration);
}

Test(filterx_cow, test_filterx_cow_child_objects_are_refs_too)
{
  FilterXObject *r =
    filterx_object_from_json("{\"a\":\"a\",\"b\":\"b\",\"c\":{\"ca\":\"ca\",\"cb\":\"cb\",\"cc\":{\"cca\":\"cca\",\"ccb\":\"ccb\",\"ccc\":\"ccc\"}}}",
                             -1, NULL);

  cr_assert(filterx_object_is_ref(r));
  FilterXObject *c = filterx_object_getattr(r, _attr_string("c"));
  cr_assert(filterx_object_is_ref(c));
  FilterXObject *cc = filterx_object_getattr(c, _attr_string("cc"));
  cr_assert(filterx_object_is_ref(cc));
  FilterXObject *ccc = filterx_object_getattr(cc, _attr_string("ccc"));
  cr_assert(filterx_object_is_type(ccc, &FILTERX_TYPE_NAME(string)));

  filterx_object_unref(ccc);
  filterx_object_unref(cc);
  filterx_object_unref(c);
  filterx_object_unref(r);
}

Test(filterx_cow, test_filterx_cow_fork_creates_a_second_reference_to_the_same_object)
{
  FilterXObject *r =
    filterx_object_from_json("{\"a\":\"a\",\"b\":\"b\",\"c\":{\"ca\":\"ca\",\"cb\":\"cb\",\"cc\":{\"cca\":\"cca\",\"ccb\":\"ccb\",\"ccc\":\"ccc\"}}}",
                             -1, NULL);

  cr_assert(filterx_object_is_ref(r));
  FilterXObject *c = filterx_object_getattr(r, _attr_string("c"));
  cr_assert(filterx_object_is_ref(c));

  /* simulate an assignment */
  FilterXObject *c_comma = filterx_object_cow_fork(&c);

  /* c_comma is now referencing the same object but is a separate ref instance */
  cr_assert(c != c_comma);
  cr_assert(filterx_object_is_ref(c_comma));

  /* let's access the children for both c and c_comma and compare them */

  /* c first */
  FilterXObject *cc = filterx_object_getattr(c, _attr_string("cc"));
  cr_assert(filterx_object_is_ref(cc));
  FilterXObject *ccc = filterx_object_getattr(cc, _attr_string("ccc"));
  cr_assert(filterx_object_is_type(ccc, &FILTERX_TYPE_NAME(string)));

  /* c_comma next */
  FilterXObject *c_comma_c = filterx_object_getattr(c_comma, _attr_string("cc"));
  cr_assert(filterx_object_is_ref(c_comma_c));
  FilterXObject *c_comma_cc = filterx_object_getattr(c_comma_c, _attr_string("ccc"));
  cr_assert(filterx_object_is_type(c_comma_cc, &FILTERX_TYPE_NAME(string)));

  /* access through c and c_comma still refers to the same dict, so all objects are equal */
  cr_assert(filterx_ref_values_equal(c_comma_c, cc));
  cr_assert(filterx_ref_values_equal(c_comma_cc, ccc));

  filterx_object_unref(c_comma_cc);
  filterx_object_unref(c_comma_c);
  filterx_object_unref(c_comma);
  filterx_object_unref(ccc);
  filterx_object_unref(cc);
  filterx_object_unref(c);
  filterx_object_unref(r);
}

Test(filterx_cow, test_filterx_cow_make_immediate_child_writable_creates_an_unshared_dict)
{
  FilterXObject *r =
    filterx_object_from_json("{\"a\":\"a\",\"b\":\"b\",\"c\":{\"ca\":\"ca\",\"cb\":\"cb\",\"cc\":{\"cca\":\"cca\",\"ccb\":\"ccb\",\"ccc\":\"ccc\"}}}",
                             -1, NULL);

  cr_assert(filterx_object_is_ref(r));
  FilterXObject *c = filterx_object_getattr(r, _attr_string("c"));
  cr_assert(filterx_object_is_ref(c));

  /* simulate an assignment */
  FilterXObject *c_comma = filterx_object_cow_fork(&c);

  /* c_comma is now referencing the same object but is a separate ref instance */
  cr_assert(c != c_comma);
  cr_assert(filterx_object_is_ref(c_comma));

  FilterXObject *c_comma_bak = c_comma;
  cr_assert(!filterx_object_is_dirty(c_comma));

  /* the two refs share the underlying object */
  cr_assert(filterx_ref_unwrap_ro(c) == filterx_ref_unwrap_ro(c_comma));


//  /* let's mutate through c_comma */
  filterx_ref_unwrap_rw(c_comma);
//  filterx_object_cow_make_writable(&c_comma);
  /* we don't expect the ref to change, it's already our exclusive ref */
  cr_assert(c_comma == c_comma_bak);

  /* the two refs now don't share the underlying object */
  cr_assert(filterx_ref_unwrap_ro(c) != filterx_ref_unwrap_ro(c_comma));

  filterx_object_unref(c_comma);
  filterx_object_unref(c);
  filterx_object_unref(r);
}

Test(filterx_cow, test_filterx_cow_make_grandchild_writable_creates_an_unshared_tree_of_dicts_from_the_top)
{
  const gchar *orig_json =
    "{\"a\":\"a\",\"b\":\"b\",\"c\":{\"ca\":\"ca\",\"cb\":\"cb\",\"cc\":{\"cca\":\"cca\",\"ccb\":\"ccb\",\"ccc\":\"ccc\"}}}";
  FilterXObject *r = filterx_object_from_json(orig_json, -1, NULL);

  cr_assert(filterx_object_is_ref(r));
  FilterXObject *c = filterx_object_getattr(r, _attr_string("c"));
  cr_assert(filterx_object_is_ref(c));
  cr_assert(filterx_weakref_is_set(&((FilterXRef *) c)->parent_container) == TRUE);

  /* simulate an assignment */
  FilterXObject *c_comma = filterx_object_cow_fork(&c);
  cr_assert(c != c_comma);
  cr_assert(filterx_weakref_is_set(&((FilterXRef *) c_comma)->parent_container) == FALSE);

  /* cc */
  FilterXObject *cc = filterx_object_getattr(c, _attr_string("cc"));
  cr_assert(filterx_object_is_ref(cc));
  cr_assert(filterx_weakref_is_set_to(&((FilterXRef *) cc)->parent_container, c));

  /* c'c */
  FilterXObject *c_comma_c = filterx_object_getattr(c_comma, _attr_string("cc"));
  cr_assert(filterx_object_is_ref(c_comma_c));
  cr_assert(filterx_weakref_is_set_to(&((FilterXRef *) c_comma_c)->parent_container, c_comma));

  /* let's mutate through c_comma_c */
  FilterXObject *c_comma_c_bak = c_comma_c;
  filterx_ref_unwrap_rw(c_comma_c);
  /* we don't expect the ref to change, it's already our exclusive ref */
  cr_assert(c_comma_c == c_comma_c_bak);

  FilterXObject *changed_string = _value_string("ccc-changed");
  cr_assert(filterx_object_setattr(c_comma_c, _attr_string("ccc"), &changed_string) == TRUE);

  GString *r_json = scratch_buffers_alloc();
  GString *c_comma_json = scratch_buffers_alloc();
  cr_assert(filterx_object_to_json(r, r_json) == TRUE);
  cr_assert(filterx_object_to_json(c_comma, c_comma_json) == TRUE);

//  cr_assert_str_eq(c_comma_json->str, orig_json);
  cr_assert_str_eq(r_json->str, orig_json);

#if 0

  /* c_comma is now referencing the same object but is a separate ref instance */
  cr_assert(c != c_comma);
  cr_assert(filterx_object_is_ref(c_comma));

  FilterXObject *c_comma_bak = c_comma;
  cr_assert(!filterx_object_is_dirty(c_comma));
  /* let's mutate through c_comma */
  filterx_object_cow_make_writable(&c_comma);
  /* we don't expect the ref to change, it's already our exclusive ref */
  cr_assert(c_comma == c_comma_bak);
  cr_assert(filterx_object_is_dirty(c_comma));


#if 0

  /* c first */
  FilterXObject *cc = filterx_object_getattr(c, _attr_string("cc"));
  cr_assert(filterx_object_is_ref(cc));

  /* let's access the children for both c and c_comma and compare them */

  FilterXObject *ccc = filterx_object_getattr(cc, _attr_string("ccc"));
  cr_assert(filterx_object_is_type(ccc, &FILTERX_TYPE_NAME(string)));


  /* access through c and c_comma still refers to the same dict, so all objects are equal */
  cr_assert(c_comma_c == cc);
  cr_assert(c_comma_cc == ccc);

  filterx_object_unref(c_comma_cc);
  filterx_object_unref(c_comma_c);
  filterx_object_unref(ccc);
  filterx_object_unref(cc);
  filterx_object_unref(c);
#endif
#endif
  filterx_object_unref(c_comma_c);
  filterx_object_unref(c_comma);
  filterx_object_unref(r);
}

static void
setup(void)
{
  app_startup();

  configuration = cfg_new_snippet();
  filterx_eval_begin_compile(&compile_context, configuration);
}

static void
teardown(void)
{
  filterx_eval_end_compile(&compile_context);
  cfg_free(configuration);
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(filterx_cow, .init = setup, .fini = teardown);

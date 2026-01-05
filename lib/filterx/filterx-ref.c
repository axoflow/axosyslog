/*
 * Copyright (c) 2024 László Várady
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

#include "filterx-object.h"
#include "filterx-eval.h"

/*
 * Copy-on-write architecture
 * ==========================
 *
 * A key part of our performance is the copy-on-write architecture
 * implemented for mutable objects.  This description tries to capture the
 * most important aspects how it works.  Copy-on-write is unfortunately a
 * complex beast and its implementation is scattered throughout the filterx
 * implementation.
 *
 * Glossary
 * --------
 *  - FilterXRef or xref:
 *  - false sharing
 *  - mutable structure
 *  - parent_container
 *  - bare objects
 *  - floating xref
 *
 * References vs. copying
 * ----------------------
 * The filterx language does not have "references" in the user-visible
 * language (not even for mutable data types), rather it **copies** data
 * where more usual languages would just store a reference.
 *
 * For example, Python or JavaScript:
 *
 *    a = {"foo": "foovalue"};
 *    b = {"bar": "barvalue"};
 *    a.b = b;
 *    b.baz = "bazvalue";
 *
 * In JavaScript or Python the assignment to `b.baz` would also change
 * `a.b.baz` as `b` is stored as a reference in the `a` dict, e.g.  the
 * values would be like:
 *
 *    a = {"foo": "foovalue","b":{"bar": "barvalue", "baz": "bazvalue"}}
 *    b = {"bar":"barvalue", "baz": "bazvalue"}
 *
 * NOTE: `a` changed along with `b`.
 *
 * The problem with this approach is that our language is primarily designed
 * for data manipulation, this kind of sharing is not at all intuitive for
 * users that are not experienced programmers.  Also, when routing data, we
 * might need to backtrack to a specific state and perform an alternative
 * route with a different set of mutations, which means that we need to get
 * back to a specific state.  This is difficult to do if we would be using
 * references.
 *
 * With all that said, this is how the assignments above would look like in
 * FilterX:
 *
 *    a = {"foo": "foovalue","b":{"bar": "barvalue"}}
 *    b = {"bar":"barvalue", "baz": "bazvalue"}
 *
 * E.g. even though `b` has changed, `a` did not.
 *
 * The need for copy-on-write
 * --------------------------
 * If we actually copied data on all assignments, that would be slow.
 * Therefore, the FilterX interpreter implements a copy-on-write mechanism
 * for mutable data types.
 *
 * NOTE: copy-on-write does not apply to immutable data types (e.g.
 * FilterXString or FilterXPrimitive), since they never change, thus sharing
 * immutable object instances is perfectly fine.
 *
 * Copy-on-write basically means that we attempt to defer the actual copying
 * of the data structures for as long as we can: we only copy the actual
 * data when a change would cascade to other dict/list objects through the
 * sharing of data structures.
 *
 * Design principles
 * -----------------
 * In case of mutable objects, we have two interoperating object classes:
 *   - a bare, mutable object class to represent the data structure (e.g.
 *     FilterXDictObject or FilterXListObject)
 *
 *   - FilterXRef (also sometimes referenced as "xref", which is different
 *     from a simple ref).  An instance of FilterXRef "contains" or "wraps"
 *     an object using its `FilterXRef->value` member and we can have
 *     multiple of these xrefs.
 *
 * All objects in FilterX are reference counted (including FilterXRef too!),
 * but FilterXRefs (xrefs) are not simple reference counts: they represent
 * the multiple "copies" of a mutable data structure, while sharing the
 * same object instance between the copies.
 *
 * FilterXRef mechanism
 * --------------------
 * An object behind a FilterXRef (xref) offers the same interface as the
 * object itself, however the object and the associated xrefs are all
 * distinct object instances.
 *
 * Wrapping an object into an xref represents another "copy" of the same
 * object.  An object is wrapped into a new xref using the following
 * functions:
 *
 *   1) a call to filterx_object_cow_prepare(), that is used to prepare a
 *      mutable object to be used by the copy-on-write mechanism.  This is
 *      only used for the creation of the very first xref pointing to a
 *      newly created object.
 *
 *   2) filterx_object_cow_fork() and fork2() are to be used when the filterx
 *      language is using a copying construct (e.g.  assigmnent, setattr or
 *      set_subscript).  These functions return both "copies" of the object:
 *      old and new.  They also handle filterx_object_cow_prepare() if an
 *      object is not yet wrapped.
 *
 *   2) filterx_object_copy() does not explicitly wrap a bare object,
 *      but will clone the xref if it is already wrapped.
 *
 * The number of copies for objects are tracked by the
 * `FilterXObject->fx_ref_cnt` member, which counts the number of
 * active xrefs to an object.
 *
 * When the reference count for an xref drops to zero, it automatically
 * removes the associated "copy" as well, e.g.  the `fx_ref_cnt` member will
 * be decreased.
 *
 * Triggering a copy
 * -----------------
 * All changes to objects are facilitated through one of the FilterXRef
 * instances (e.g. setattr, set_subscript or another mutating method).
 *
 * When a mutating method is called, the xref would first trigger a clone of
 * the underlying object and then apply the requested operation on the
 * clone.  The clone operation removes the sharing of the objects between the
 * two xrefs.
 *
 * This is implemented by the _filterx_ref_cow() function.
 *
 * Recursive data structures
 * -------------------------
 * Mutable data structures often contain other mutable data structures as
 * elements, thereby representing a recursive data structure like JSON (dict
 * in dict/list or list in dict/list)
 *
 * In case of a recursive structure, copy-on-write operates layer-by-layer.
 * It may happen that the top-level dict had already been unshared due to a
 * write operation, while most of the children are still shared.
 *
 * We support cases where we look up a child of a large data structure
 * (think of results looked up from cache_json_file()) and sharing that as
 * long as the copy is not changed.
 *
 * Parent tracking
 * ---------------
 *
 * Given a recursive data structure, changing any of the children, even
 * those buried deeply in the data structure means that the entire hierarchy
 * (up to the top-level element) should be considered changed.
 *
 * The reason is simple: a changed child needs to be updated in the
 * container, which is a child of its container, etc.  Basically any "clone"
 * operation is carried out all the way up to the top
 * (`_filterx_ref_cow_parents()` function).
 *
 * In order to carry out these update operations, we need to keep track of
 * the parent container.  This is done by the `FilterXRef->parent_container`
 * weakref.  That means that all hierarchical data structures can be
 * navigated from top-to-bottom (using normal accessor functions like
 * getattr or get_subscript) and from bottom-to-up, using the
 * parent_container weakref.
 *
 * Since we have this weakref, we also use it to mark the top-level dict as
 * dirty, whenever any of the children are changed.
 *
 * Sharing of xrefs
 * ----------------
 * The idea of FilterXRefs (or xrefs) is to represent a distinct copy of a
 * mutable data structure.  There's an exception to everything though: and
 * that is the potential sharing of xrefs between multiple shared object
 * hierarchies.
 *
 * FilterXRef instances can be shared when we have a dict embedded in
 * another dict (e.g.  top-level and a child) and the top-level dict is
 * being shared by two xrefs.  In this case, while the xref to the top-level
 * is distinct (kept in two variables for example), the xref to the child
 * (contained in the top-level as a value) is shared, and the child dict
 * itself may contain additional xrefs to further mutable data objects.
 *
 * When manipulating data structures through xrefs we have to know if those
 * xrefs are shared or not.  Actually we can't mutate objects through shared
 * xrefs, as that would mean that changes would cascade into multiple
 * objects, or worse, we can even create an infinite recursion in our
 * hierarchy.
 *
 * To avoid this, xref sharing needs to be eliminated.
 *
 * Floating xrefs
 * --------------
 * As we navigate and change the hierarchy of dict/list instances while
 * interpreting the filterx statements, we need to notice and eliminate the
 * use of shared xrefs.  If an xref is shared, that means that `fx_ref_cnt`
 * of the associated object does not correctly reflect the number of total
 * copies (xrefs) of the object, which in turn would obviously break our
 * copy-on-write mechanism.
 *
 * An xref is considered shared, iff:
 *  - its parent_container is pointing outside of the current hierarchy, OR
 *  - the parent container is shared (e.g. `fx_ref_cnt` > 1),
 *
 * Any expr that evaluates to an object in a hierarchical structure (e.g.
 * getattr or get_subscript) must check for shared xrefs and actively
 * replace them with a "floating xref" instead of returning the stored xref.
 * See the function
 * `_filterx_ref_replace_shared_xref_with_a_floating_one()`.
 *
 * A floating xref is a temporary FilterXRef that is explicitly marked as
 * floating using the `filterx_ref_floating()` function (and the associated
 * FILTERX_REF_FLAG_FLOATING flag).
 *
 * Floating xrefs are yet to be stored somewhere or be discarded at the end
 * of the FilterX statement unless they are actually needed.  A floating xref
 * should only be kept in local variables and returned by
 * filterx_expr_eval().  They should never cross statement boundaries in the
 * FilterX language.
 *
 * If a floating xref is eventually stored (using setattr/set_subscript or
 * assigning it to a variable), it becomes grounded, e.g.  a normal xref
 * that is kept around.
 *
 * When do we generate floating xrefs
 * -----------------------------------
 *
 *   1) when a hierarchical structure is sharing an xref with another, then
 *      the getattr/get_subscript operation returns a floating xref instead of
 *      the stored (& shared) one.
 *
 *   2) `filterx_object_cow_fork2()` generates a floating ref as the new copy
 *
 *   3) the new `move()` FilterX "function" retrieves the stored xref, unsets
 *      it and returns it as a floating xref.
 *
 * When do we ground floating xrefs
 * --------------------------------
 *
 *   1) when storing a value in a list/dict `filterx_object_cow_store()`
 *
 *   2) filterx_object_copy() turns the floating xref into a simple xref,
 *      without generating a new clone.
 *
 */
static FilterXObject *
_filterx_ref_clone(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;

  g_assert(s->floating_ref == FALSE);
  return _filterx_ref_new(filterx_object_ref(self->value));
}

static inline void
_filterx_ref_clone_value_if_shared(FilterXRef *self, FilterXRef *child_of_interest)
{
  if (g_atomic_counter_get(&self->value->fx_ref_cnt) <= 1)
    return;

  FilterXObject *cloned = filterx_object_clone_container(self->value, &self->super, &child_of_interest->super);

  g_atomic_counter_dec_and_test(&self->value->fx_ref_cnt);
  filterx_object_unref(self->value);

  self->value = cloned;
  g_atomic_counter_inc(&self->value->fx_ref_cnt);
}

gboolean
_filterx_ref_cow_parents(FilterXObject *s, gpointer user_data)
{
  FilterXRef *self = (FilterXRef *) s;
  FilterXRef *child_of_interest = (FilterXRef *) user_data;

  filterx_weakref_invoke(&self->parent_container, _filterx_ref_cow_parents, self);
  _filterx_ref_clone_value_if_shared(self, child_of_interest);
  filterx_object_set_dirty(&self->super, TRUE);
  return TRUE;
}

/* mutator methods */

static gboolean
_filterx_ref_setattr(FilterXObject *s, FilterXObject *attr, FilterXObject **new_value)
{
  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);

  gboolean result = filterx_object_setattr(self->value, attr, new_value);
  if (result)
    filterx_ref_set_parent_container(*new_value, s);
  return result;
}

static gboolean
_filterx_ref_set_subscript(FilterXObject *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);

  gboolean result = filterx_object_set_subscript(self->value, key, new_value);
  if (result)
    filterx_ref_set_parent_container(*new_value, s);
  return result;
}

static gboolean
_filterx_ref_unset_key(FilterXObject *s, FilterXObject *key)
{
  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);

  return filterx_object_unset_key(self->value, key);
}

static FilterXObject *
_filterx_ref_move_key(FilterXObject *s, FilterXObject *key)
{
  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);

  return filterx_object_move_key(self->value, key);
}

static void
_filterx_ref_free(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;

  g_atomic_counter_dec_and_test(&self->value->fx_ref_cnt);
  filterx_object_unref(self->value);

  filterx_object_free_method(s);
}

static void
_filterx_ref_make_readonly(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;

  filterx_object_make_readonly(self->value);
}

static void
_filterx_ref_freeze(FilterXObject **pself, FilterXObjectFreezer *freezer)
{
  FilterXRef *self = (FilterXRef *) *pself;

  FilterXObject *orig_value = self->value;
  filterx_object_freezer_keep(freezer, *pself);
  filterx_object_freeze(&self->value, freezer);

  /* Mutable objects themselves should never be deduplicated,
   * only immutable values INSIDE those recursive mutable objects.
   */
  g_assert(orig_value == self->value);
}

/* readonly methods */

static gboolean
_filterx_ref_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXRef *self = (FilterXRef *) s;

  if (self->value->type->marshal)
    return self->value->type->marshal(self->value, repr, t);

  return FALSE;
}

static gboolean
_filterx_ref_format_json_append(FilterXObject *s, GString *json)
{
  FilterXRef *self = (FilterXRef *) s;
  return filterx_object_format_json_append(self->value, json);
}

static gboolean
_filterx_ref_truthy(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;
  return filterx_object_truthy(self->value);
}

/*
 * Sometimes we are looking up values from a dict still shared between
 * multiple, independent copy-on-write hierarchies (see xref sharing in the
 * long comment at the top).  In these cases, the xref we retrieve as a
 * result of the lookup points outside of our current structure or its
 * parent itself is shared.
 *
 * In these cases, we need a new ref instance, which has the proper
 * parent_container value, while still cotinuing to share the dict (e.g.
 * self->value).
 *
 * In case we are just using the values in a read-only manner, the dicts
 * will not be cloned, the ref will be freed once we are done using it.
 *
 * In case we are actually mutating the dict, the new "floating" ref will
 * cause the dict to be cloned and by that time our floating ref will find
 * its proper home: in the parent dict.  This is exactly the
 * "child_of_interest" argument we are passing to clone_container().
 */
static FilterXObject *
_filterx_ref_replace_shared_xref_with_a_floating_one(FilterXObject *s, FilterXObject *c)
{
  if (!s || !filterx_object_is_ref(s))
    return s;

  FilterXRef *self = (FilterXRef *) s;
  FilterXRef *container = (FilterXRef *) c;

  if (filterx_weakref_is_set_to(&self->parent_container, &container->super) &&
      g_atomic_counter_get(&container->value->fx_ref_cnt) <= 1)
    return s;

  FilterXObject *result = filterx_ref_float(_filterx_ref_new(filterx_object_ref(self->value)));
  filterx_object_unref(&self->super);
  filterx_ref_set_parent_container(result, &container->super);
  return result;
}

static FilterXObject *
_filterx_ref_getattr(FilterXObject *s, FilterXObject *attr)
{
  FilterXRef *self = (FilterXRef *) s;
  FilterXObject *result = filterx_object_getattr(self->value, attr);
  return _filterx_ref_replace_shared_xref_with_a_floating_one(result, s);
}

static FilterXObject *
_filterx_ref_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXRef *self = (FilterXRef *) s;
  FilterXObject *result = filterx_object_get_subscript(self->value, key);
  return _filterx_ref_replace_shared_xref_with_a_floating_one(result, s);
}

static gboolean
_filterx_ref_is_key_set(FilterXObject *s, FilterXObject *key)
{
  FilterXRef *self = (FilterXRef *) s;
  return filterx_object_is_key_set(self->value, key);
}

static gboolean
_filterx_ref_iter(FilterXObject *s, FilterXObjectIterFunc func, gpointer user_data)
{
  FilterXRef *self = (FilterXRef *) s;
  return filterx_object_iter(self->value, func, user_data);
}

static gboolean
_filterx_ref_repr_append(FilterXObject *s, GString *repr)
{
  FilterXRef *self = (FilterXRef *) s;

  if (self->value->type->repr)
    return self->value->type->repr(self->value, repr);
  return FALSE;
}

static gboolean
_filterx_ref_str_append(FilterXObject *s, GString *str)
{
  FilterXRef *self = (FilterXRef *) s;

  if (self->value->type->str)
    return self->value->type->str(self->value, str);
  return _filterx_ref_repr_append(s, str);
}

static gboolean
_filterx_ref_len(FilterXObject *s, guint64 *len)
{
  FilterXRef *self = (FilterXRef *) s;
  return filterx_object_len(self->value, len);
}

static FilterXObject *
_filterx_ref_add(FilterXObject *s, FilterXObject *object)
{
  FilterXRef *self = (FilterXRef *) s;
  return filterx_object_add(self->value, object);
}

static FilterXObject *
_filterx_ref_add_inplace(FilterXObject *s, FilterXObject *object)
{
  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);
  FilterXObject *new_object = filterx_object_add_inplace(self->value, object);
  if (!new_object)
    return NULL;

  if (filterx_object_is_ref(new_object))
    return new_object;

  if (new_object != self->value)
    {
      filterx_object_unref(self->value);
      self->value = new_object;
    }
  else
    {
      filterx_object_unref(new_object);
    }
  return filterx_object_ref(s);
}

/* NOTE: fastpath is in the header as an inline function */
FilterXObject *
_filterx_ref_new(FilterXObject *value)
{
#if SYSLOG_NG_ENABLE_DEBUG
  if (!value->type->is_mutable || filterx_object_is_ref(value))
    g_assert("filterx_ref_new() must only be used for a cowable object" && FALSE);
#endif
//  FilterXRef *self = g_new0(FilterXRef, 1);
  FilterXRef *self = filterx_new_object(FilterXRef);

  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(ref));
  self->super.readonly = FALSE;

  self->value = value;
  g_atomic_counter_inc(&self->value->fx_ref_cnt);

  return &self->super;
}

FILTERX_DEFINE_TYPE(ref, FILTERX_TYPE_NAME(object),
                    .is_mutable = TRUE,
                    .marshal = _filterx_ref_marshal,
                    .clone = _filterx_ref_clone,
                    .format_json = _filterx_ref_format_json_append,
                    .truthy = _filterx_ref_truthy,
                    .getattr = _filterx_ref_getattr,
                    .setattr = _filterx_ref_setattr,
                    .get_subscript = _filterx_ref_get_subscript,
                    .set_subscript = _filterx_ref_set_subscript,
                    .is_key_set = _filterx_ref_is_key_set,
                    .unset_key = _filterx_ref_unset_key,
                    .move_key = _filterx_ref_move_key,
                    .iter = _filterx_ref_iter,
                    .repr = _filterx_ref_repr_append,
                    .str = _filterx_ref_str_append,
                    .len = _filterx_ref_len,
                    .add = _filterx_ref_add,
                    .add_inplace = _filterx_ref_add_inplace,
                    .make_readonly = _filterx_ref_make_readonly,
                    .freeze = _filterx_ref_freeze,
                    .free_fn = _filterx_ref_free,
                   );

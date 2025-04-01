/*
 * Copyright (c) 2024 László Várady
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

#include "filterx-object.h"

static FilterXObject *
_filterx_ref_clone(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;

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

static void
_filterx_ref_cow_parents(FilterXRef *self, FilterXRef *child_of_interest)
{
  FilterXRef *parent_container = (FilterXRef *) filterx_weakref_get(&self->parent_container);

  if (parent_container)
    {
      _filterx_ref_cow_parents(parent_container, self);
      filterx_object_unref(&parent_container->super);

    }
  _filterx_ref_clone_value_if_shared(self, child_of_interest);
  filterx_object_set_dirty(&self->super, TRUE);
}

void
_filterx_ref_cow(FilterXRef *self)
{
  _filterx_ref_cow_parents(self, NULL);
}

/* mutator methods */

static gboolean
_filterx_ref_setattr(FilterXObject *s, FilterXObject *attr, FilterXObject **new_value)
{
  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);

  gboolean result = filterx_object_setattr(self->value, attr, new_value);
  filterx_ref_set_parent_container(*new_value, s);
  return result;
}

static gboolean
_filterx_ref_set_subscript(FilterXObject *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);

  gboolean result = filterx_object_set_subscript(self->value, key, new_value);
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

static void
_filterx_ref_free(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;

  g_atomic_counter_dec_and_test(&self->value->fx_ref_cnt);
  filterx_object_unref(self->value);
  filterx_object_free_method(s);
}

static void
_filterx_make_readonly(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;

  filterx_object_make_readonly(self->value);
}

static void
_filterx_ref_freeze(FilterXObject **s)
{
  FilterXRef *self = (FilterXRef *) *s;

  filterx_object_freeze(&self->value);
}

static void
_filterx_ref_unfreeze(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;

  filterx_object_unfreeze(self->value);
}

/* readonly methods */

static FilterXObject *
_filterx_ref_unmarshal(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;
  FilterXObject *unmarshalled = filterx_object_unmarshal(self->value);
  if (unmarshalled != self->value)
    return unmarshalled;

  filterx_object_unref(self->value);
  return filterx_object_ref(s);
}

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
 * multiple, independent copy-on-write hierarchies.  In these cases, the ref
 * we retrieve as a result of the lookup points outside of our current
 * structure.
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
 * "child_of_interest" we are passing to clone_container().
 */
static void
_filterx_ref_replace_foreign_ref_with_a_floating_one(FilterXObject **ps, FilterXObject *container)
{
  if (!(*ps))
    return;

  if (!filterx_object_is_ref(*ps))
    return;

  FilterXRef *self = (FilterXRef *) *ps;

  if (filterx_weakref_is_set_to(&self->parent_container, container))
    return;

  *ps = _filterx_ref_new(filterx_object_ref(self->value));
  filterx_object_unref(&self->super);
  filterx_ref_set_parent_container(*ps, container);
}

static FilterXObject *
_filterx_ref_getattr(FilterXObject *s, FilterXObject *attr)
{
  FilterXRef *self = (FilterXRef *) s;
  FilterXObject *result = filterx_object_getattr(self->value, attr);
  _filterx_ref_replace_foreign_ref_with_a_floating_one(&result, s);
  return result;
}

static FilterXObject *
_filterx_ref_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXRef *self = (FilterXRef *) s;
  FilterXObject *result = filterx_object_get_subscript(self->value, key);
  _filterx_ref_replace_foreign_ref_with_a_floating_one(&result, s);
  return result;
}

static gboolean
_filterx_ref_is_key_set(FilterXObject *s, FilterXObject *key)
{
  FilterXRef *self = (FilterXRef *) s;
  return filterx_object_is_key_set(self->value, key);
}

static FilterXObject *
_filterx_ref_list_factory(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;
  FilterXObject *list = filterx_object_create_list(self->value);
  filterx_object_cow_wrap(&list);
  filterx_ref_set_parent_container(list, s);
  return list;
}

static FilterXObject *
_filterx_ref_dict_factory(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;
  FilterXObject *dict = filterx_object_create_dict(self->value);
  filterx_object_cow_wrap(&dict);
  filterx_ref_set_parent_container(dict, s);
  return dict;
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
  return filterx_object_add_object(self->value, object);
}

/* NOTE: fastpath is in the header as an inline function */
FilterXObject *
_filterx_ref_new(FilterXObject *value)
{
  if (filterx_object_is_type(value, &FILTERX_TYPE_NAME(ref)))
    {
      FilterXRef *absorbed_ref = (FilterXRef *) value;
      value = filterx_object_ref(absorbed_ref->value);
      filterx_object_unref(&absorbed_ref->super);
    }

  FilterXRef *self = g_new0(FilterXRef, 1);

  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(ref));
  self->super.readonly = FALSE;

  self->value = value;
  g_atomic_counter_inc(&self->value->fx_ref_cnt);

  return &self->super;
}

FILTERX_DEFINE_TYPE(ref, FILTERX_TYPE_NAME(object),
                    .is_mutable = TRUE,
                    .unmarshal = _filterx_ref_unmarshal,
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
                    .list_factory = _filterx_ref_list_factory,
                    .dict_factory = _filterx_ref_dict_factory,
                    .repr = _filterx_ref_repr_append,
                    .str = _filterx_ref_str_append,
                    .len = _filterx_ref_len,
                    .add = _filterx_ref_add,
                    .make_readonly = _filterx_make_readonly,
                    .freeze = _filterx_ref_freeze,
                    .unfreeze = _filterx_ref_unfreeze,
                    .free_fn = _filterx_ref_free,
                   );

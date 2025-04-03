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
#ifndef FILTERX_OBJECT_H_INCLUDED
#define FILTERX_OBJECT_H_INCLUDED

#include "logmsg/logmsg.h"
#include "compat/json.h"
#include "atomic.h"

typedef struct _FilterXType FilterXType;
typedef struct _FilterXObject FilterXObject;
typedef struct _FilterXRef FilterXRef;
typedef struct _FilterXExpr FilterXExpr;

struct _FilterXType
{
  FilterXType *super_type;
  const gchar *name;
  gboolean is_mutable;

  /* WARNING: adding a new method here requires an implementation in FilterXRef as well */
  FilterXObject *(*unmarshal)(FilterXObject *self);
  gboolean (*marshal)(FilterXObject *self, GString *repr, LogMessageValueType *t);
  FilterXObject *(*clone)(FilterXObject *self);
  FilterXObject *(*clone_container)(FilterXObject *self, FilterXObject *container, FilterXObject *child_of_interest);
  gboolean (*truthy)(FilterXObject *self);
  FilterXObject *(*getattr)(FilterXObject *self, FilterXObject *attr);
  gboolean (*setattr)(FilterXObject *self, FilterXObject *attr, FilterXObject **new_value);
  FilterXObject *(*get_subscript)(FilterXObject *self, FilterXObject *key);
  gboolean (*set_subscript)(FilterXObject *self, FilterXObject *key, FilterXObject **new_value);
  gboolean (*is_key_set)(FilterXObject *self, FilterXObject *key);
  gboolean (*unset_key)(FilterXObject *self, FilterXObject *key);
  FilterXObject *(*list_factory)(FilterXObject *self);
  FilterXObject *(*dict_factory)(FilterXObject *self);
  gboolean (*format_json)(FilterXObject *self, GString *json);
  gboolean (*repr)(FilterXObject *self, GString *repr);
  gboolean (*str)(FilterXObject *self, GString *str);
  gboolean (*len)(FilterXObject *self, guint64 *len);
  FilterXObject *(*add)(FilterXObject *self, FilterXObject *object);
  void (*make_readonly)(FilterXObject *self);
  void (*freeze)(FilterXObject **self);
  void (*unfreeze)(FilterXObject *self);
  void (*free_fn)(FilterXObject *self);
};

void filterx_type_init(FilterXType *type);
void _filterx_type_init_methods(FilterXType *type);

#define FILTERX_TYPE_NAME(_name) filterx_type_ ## _name
#define FILTERX_DECLARE_TYPE(_name) \
    extern FilterXType FILTERX_TYPE_NAME(_name)
#define FILTERX_DEFINE_TYPE(_name, _super_type, ...) \
    FilterXType FILTERX_TYPE_NAME(_name) =  \
    {           \
      .super_type = &_super_type,   \
      .name = # _name,        \
      __VA_ARGS__       \
    }

FILTERX_DECLARE_TYPE(object);
FILTERX_DECLARE_TYPE(ref);

static inline gboolean
_filterx_type_is_referenceable(FilterXType *t)
{
  return t->is_mutable && t != &FILTERX_TYPE_NAME(ref);
}

/* FilterXObject refcount ranges.  The ref count for the objects is special
 * to allow multiple allocation strategies:
 *
 * 1) heap allocated, normal refcounting
 *
 *    Most objects will be in this category, ref_cnt starts with 1,
 *    ref/unref increments and decrements the refcount respectively.
 *
 *    The ref_cnt will be in this range: (0, FILTERX_OBJECT_REFCOUNT_OFLOW_MARK)
 *
 *    The ref_cnt overflow is detected by leaving a gap between the overflow
 *    mark and the first special value, which is large enough to avoid races
 *    stepping through the range
 *
 * 2) stack allocated, no refcounting
 *
 *    Local values can be stored on the stack, to avoid a heap allocation,
 *    these values are only used by a single thread.
 *
 *    The ref_cnt is always equal to FILTERX_OBJECT_REFCOUNT_STACK
 *
 * 3) heap allocated, frozen
 *
 *    Objects frozen before the evaluation starts (e.g.  cache_json_file or
 *    other cached objects).
 *
 *    The normal ref/unref operations are noops, e.g.  everything just
 *    assumes these objects will exist for as long as necessary.
 *
 *    The freeze/unfreeze operations increment and decrement the refcount
 *    but ensure that it remains in this range.  Multi-threaded, read only
 *    access to frozen objects is possible during evaluation, but
 *    freeze/unfreeze cannot be used from multiple threads in parallel.
 *
 *    The ref_cnt will be in this range: [FILTERX_OBJECT_REFCOUNT_FROZEN, G_MAXINT32]
 *
 */
enum
{
  FILTERX_OBJECT_REFCOUNT_OFLOW_MARK=G_MAXINT32/2,
  FILTERX_OBJECT_REFCOUNT_OFLOW_RANGE_MAX=FILTERX_OBJECT_REFCOUNT_OFLOW_MARK + 1024,
  /* stack based allocation */
  FILTERX_OBJECT_REFCOUNT_STACK,

  /* frozen objects have a refcount that is larger than equal to FILTERX_OBJECT_REFCOUNT_FROZEN, normal ref/unref does not change these refcounts
   * but freeze/unfreeze does */
  FILTERX_OBJECT_REFCOUNT_FROZEN,
};


struct _FilterXObject
{
  GAtomicCounter ref_cnt;
  GAtomicCounter fx_ref_cnt;

  /* NOTE:
   *
   *     readonly          -- marks the object as unmodifiable,
   *                          propagates to the inner elements lazily
   *
   *     weak_referenced   -- marks that this object is referenced via a at
   *                          least one weakref already.
   *
   *     is_dirty          -- marks that the object was changed (mutable objects only)
   *
   */
  guint readonly:1, weak_referenced:1, is_dirty:1;
  FilterXType *type;
};

static inline gboolean
_filterx_object_is_type(FilterXObject *object, FilterXType *type)
{
  FilterXType *self_type = object->type;
  while (self_type)
    {
      if (type == self_type)
        return TRUE;
      self_type = self_type->super_type;
    }
  return FALSE;
}

static inline gboolean
filterx_object_is_type(FilterXObject *object, FilterXType *type)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(!(_filterx_type_is_referenceable(type) && _filterx_object_is_type(object, &FILTERX_TYPE_NAME(ref)))
           && "filterx_ref_unwrap() must be used before comparing to mutable types");
#endif

  return _filterx_object_is_type(object, type);
}

FilterXObject *filterx_object_getattr_string(FilterXObject *self, const gchar *attr_name);
gboolean filterx_object_setattr_string(FilterXObject *self, const gchar *attr_name, FilterXObject **new_value);

FilterXObject *filterx_object_new(FilterXType *type);
void filterx_object_freeze(FilterXObject **pself);
void filterx_object_unfreeze(FilterXObject *self);
void filterx_object_unfreeze_and_free(FilterXObject *self);
void filterx_object_init_instance(FilterXObject *self, FilterXType *type);
void filterx_object_free_method(FilterXObject *self);

void filterx_json_associate_cached_object(struct json_object *jso, FilterXObject *filterx_object);

static inline gboolean
filterx_object_is_frozen(FilterXObject *self)
{
  return g_atomic_counter_get(&self->ref_cnt) >= FILTERX_OBJECT_REFCOUNT_FROZEN;
}

static inline FilterXObject *
filterx_object_ref(FilterXObject *self)
{
  if (!self)
    return NULL;

  gint r = g_atomic_counter_get(&self->ref_cnt);
  if (r < FILTERX_OBJECT_REFCOUNT_OFLOW_MARK && r > 0)
    {
      /* NOTE: getting into this path is racy, as two threads might be
       * checking the overflow mark in parallel and then decide we need to
       * run this (normal) path.  In this case, the race could cause ref_cnt
       * to reach FILTERX_OBJECT_REFCOUNT_OFLOW_MARK, without triggering the
       * overflow assert below.
       *
       * To mitigate this, FILTERX_OBJECT_REFCOUNT_OFLOW_MARK is set to 1024
       * less than the first value that we handle specially.  This means
       * that even if the race is lost, we would need 1024 competing CPUs
       * concurrently losing the race and incrementing ref_cnt here.  And
       * even in this case the only issue is that we don't detect an actual
       * overflow at runtime that should never occur in the first place.
       *
       * This is _really_ unlikely, and we will detect ref_cnt overflows in
       * non-doom scenarios first, so we can address the actual issue (which
       * might be a reference counting bug somewhere).
       *
       * If less than 1024 CPUs lose the race, then the refcount would end
       * up in the range between FILTERX_OBJECT_REFCOUNT_OFLOW_MARK and
       * FILTERX_OBJECT_REFCOUNT_STACK, causing the assertion at the end of
       * this function to trigger an abort.
       *
       * The non-racy solution would be to use a
       * g_atomic_int_exchange_and_add() call and checking the old_value
       * against FILTERX_OBJECT_REFCOUNT_OFLOW_MARK another time, but that's
       * an extra conditional in a hot-path.
       */

      g_atomic_counter_inc(&self->ref_cnt);
      return self;
    }

  if (r == FILTERX_OBJECT_REFCOUNT_STACK)
    {
      /* we can't use filterx_object_clone() directly, as that's an inline
       * function declared further below.  Also, filterx_object_clone() does
       * not clone inmutable objects.  We only support allocating inmutable
       * objects on the stack */
      return self->type->clone(self);
    }

  if (r >= FILTERX_OBJECT_REFCOUNT_FROZEN)
    return self;

  g_assert_not_reached();
}

static inline void
filterx_object_unref(FilterXObject *self)
{
  if (!self)
    return;

  gint r = g_atomic_counter_get(&self->ref_cnt);
  if (r == FILTERX_OBJECT_REFCOUNT_STACK)
    {
      /* NOTE: Normally, stack based allocations are only used by a single
       * thread.  Furthermore, code where we use this object will only have
       * a borrowed reference, e.g.  it can't cross thread boundaries.  With
       * that said, even though the condition of this if() statement is
       * racy, it's not actually racing, as we only have a single relevant
       * thread.
       *
       * In the rare case where we do want to pass a stack based allocation
       * to another thread, we would need to pass a new reference to it, and
       * filterx_object_ref() clones a new object in this case, which again
       * means, that we won't have actual race here.
       */

      g_atomic_counter_set(&self->ref_cnt, 0);
      return;
    }
  if (r >= FILTERX_OBJECT_REFCOUNT_FROZEN)
    return;
  if (r <= 0)
    g_assert_not_reached();

  if (g_atomic_counter_dec_and_test(&self->ref_cnt))
    {
      self->type->free_fn(self);
      g_free(self);
    }
}

static inline void
filterx_object_make_readonly(FilterXObject *self)
{
  if (self->type->make_readonly)
    self->type->make_readonly(self);

  self->readonly = TRUE;
}

static inline FilterXObject *
filterx_object_unmarshal(FilterXObject *self)
{
  if (self->type->unmarshal)
    return self->type->unmarshal(self);
  return filterx_object_ref(self);
}

static inline gboolean
filterx_object_repr(FilterXObject *self, GString *repr)
{
  if (self->type->repr)
    {
      g_string_truncate(repr, 0);
      return self->type->repr(self, repr);
    }
  return FALSE;
}

static inline gboolean
filterx_object_repr_append(FilterXObject *self, GString *repr)
{
  if (self->type->repr)
    {
      return self->type->repr(self, repr);
    }
  return FALSE;
}

static inline gboolean
filterx_object_str(FilterXObject *self, GString *str)
{
  if (self->type->str)
    {
      g_string_truncate(str, 0);
      return self->type->str(self, str);
    }
  return filterx_object_repr(self, str);
}

static inline gboolean
filterx_object_str_append(FilterXObject *self, GString *str)
{
  if (self->type->str)
    {
      return self->type->str(self, str);
    }
  return filterx_object_repr_append(self, str);
}

static inline gboolean
filterx_object_format_json(FilterXObject *self, GString *json)
{
  g_string_truncate(json, 0);
  return self->type->format_json(self, json);
}

static inline gboolean
filterx_object_format_json_append(FilterXObject *self, GString *json)
{
  return self->type->format_json(self, json);
}

static inline gboolean
filterx_object_len(FilterXObject *self, guint64 *len)
{
  if (!self->type->len)
    return FALSE;
  return self->type->len(self, len);
}

static inline gboolean
filterx_object_marshal(FilterXObject *self, GString *repr, LogMessageValueType *t)
{
  if (self->type->marshal)
    {
      g_string_truncate(repr, 0);
      return self->type->marshal(self, repr, t);
    }
  return FALSE;
}

static inline gboolean
filterx_object_marshal_append(FilterXObject *self, GString *repr, LogMessageValueType *t)
{
  if (self->type->marshal)
    return self->type->marshal(self, repr, t);
  return FALSE;
}

static inline FilterXObject *
filterx_object_clone_container(FilterXObject *self, FilterXObject *container, FilterXObject *child_of_interest)
{
  if (self->type->clone_container)
    return self->type->clone_container(self, container, child_of_interest);
  return self->type->clone(self);
}

static inline FilterXObject *
filterx_object_clone(FilterXObject *self)
{
  if (self->readonly)
    return filterx_object_ref(self);
  return self->type->clone(self);
}

static inline gboolean
filterx_object_truthy(FilterXObject *self)
{
  return self->type->truthy(self);
}

static inline gboolean
filterx_object_falsy(FilterXObject *self)
{
  return !filterx_object_truthy(self);
}

static inline FilterXObject *
filterx_object_getattr(FilterXObject *self, FilterXObject *attr)
{
  if (!self->type->getattr)
    return NULL;

  FilterXObject *result = self->type->getattr(self, attr);
  if (result && self->readonly)
    filterx_object_make_readonly(result);
  return result;
}

static inline gboolean
filterx_object_setattr(FilterXObject *self, FilterXObject *attr, FilterXObject **new_value)
{
  g_assert(!self->readonly);

  if (self->type->setattr)
    return self->type->setattr(self, attr, new_value);
  return FALSE;
}

static inline FilterXObject *
filterx_object_get_subscript(FilterXObject *self, FilterXObject *key)
{
  if (!self->type->get_subscript)
    return NULL;

  FilterXObject *result = self->type->get_subscript(self, key);
  if (result && self->readonly)
    filterx_object_make_readonly(result);
  return result;
}

static inline gboolean
filterx_object_set_subscript(FilterXObject *self, FilterXObject *key, FilterXObject **new_value)
{
  g_assert(!self->readonly);

  if (self->type->set_subscript)
    return self->type->set_subscript(self, key, new_value);
  return FALSE;
}

static inline gboolean
filterx_object_is_key_set(FilterXObject *self, FilterXObject *key)
{
  if (self->type->is_key_set)
    return self->type->is_key_set(self, key);
  return FALSE;
}

static inline gboolean
filterx_object_unset_key(FilterXObject *self, FilterXObject *key)
{
  g_assert(!self->readonly);

  if (self->type->unset_key)
    return self->type->unset_key(self, key);
  return FALSE;
}

static inline FilterXObject *
filterx_object_create_list(FilterXObject *self)
{
  if (!self->type->list_factory)
    return NULL;

  return self->type->list_factory(self);
}

static inline FilterXObject *
filterx_object_create_dict(FilterXObject *self)
{
  if (!self->type->dict_factory)
    return NULL;

  return self->type->dict_factory(self);
}

static inline FilterXObject *
filterx_object_add_object(FilterXObject *self, FilterXObject *object)
{
  if (!self->type->add)
    {
      msg_error("The add method is not supported for the given type",
                evt_tag_str("type", self->type->name));
      return NULL;
    }

  return self->type->add(self, object);
}

static inline gboolean
filterx_object_is_dirty(FilterXObject *self)
{
  return self->is_dirty;
}

static inline void
filterx_object_set_dirty(FilterXObject *self, gboolean value)
{
  self->is_dirty = value;
}

#define FILTERX_OBJECT_STACK_INIT(_type) \
  { \
    .ref_cnt = { .counter = FILTERX_OBJECT_REFCOUNT_STACK }, \
    .fx_ref_cnt = { .counter = 0 }, \
    .readonly = TRUE, \
    .weak_referenced = FALSE, \
    .is_dirty = FALSE, \
    .type = &FILTERX_TYPE_NAME(_type) \
  }

#include "filterx-ref.h"

static inline gboolean
filterx_object_is_type_or_ref(FilterXObject *object, FilterXType *type)
{
  if (_filterx_object_is_type(object, &FILTERX_TYPE_NAME(ref)))
    return _filterx_object_is_type(((FilterXRef *) object)->value, type);
  return _filterx_object_is_type(object, type);
}

/*
 * Make sure mutable objects are encapsulated into a FilterXRef.  To be
 * called as the first thing before an object can be assigned to a variable
 * or stored in a dict/list.
 *
 * NOTE: potentially replaces *value with a FilterXRef, sinking the passed
 * reference, returning a FilterXRef instead.
 */
static inline void
filterx_object_cow_wrap(FilterXObject **o)
{
  FilterXObject *value = *o;
  if (!value || value->readonly || !_filterx_type_is_referenceable(value->type))
    return;
  *o = _filterx_ref_new(value);
}


/* NOTE: potentially replaces *value with a FilterXRef, sinking the passed
 * reference.  It replaces the copied object as a return value */
static inline FilterXObject *
filterx_object_cow_fork(FilterXObject **o)
{
  filterx_object_cow_wrap(o);
  return filterx_object_clone(*o);
}

/* */
static inline FilterXObject *
filterx_object_cow_store(FilterXObject **o)
{
  filterx_object_cow_wrap(o);
  return filterx_object_ref(*o);
}

#endif

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
filterx_type_is_ref(FilterXType *type)
{
  return type == &FILTERX_TYPE_NAME(ref);
}

/* can instances of this type support copy-on-write (cow)? */
static inline gboolean
filterx_type_is_cowable(FilterXType *type)
{
  return type->is_mutable && !filterx_type_is_ref(type);
}

/* FilterXObject refcount ranges.  The ref count for the objects is special
 * to allow multiple allocation strategies:
 *
 * 1) normal objects (allocated on heap, normal refcounting)
 *
 *    Most objects will be in this category, ref_cnt starts with 1,
 *    ref/unref increments and decrements the refcount respectively.
 *
 * 2) local values (allocated on stack, no refcounting)
 *
 *    Local values can be stored on the stack, to avoid a heap allocation,
 *    these values are only used by a single thread.
 *
 *    If you need another reference to a stack based allocation, you need to
 *    clone it and that's exactly what filterx_object_ref() does for stack
 *    allocated objects.
 *
 *    The ref_cnt is always equal to FILTERX_OBJECT_REFCOUNT_STACK
 *
 * 3) hibernated objects
 *
 *    Hibernated objects are preserved for the entire lifecycle of the process.
 *    The caller is responsible for providing efficient storage and deallocation
 *    before shutdown.
 *
 *    Both the normal ref/unref and the freeze operations are noops.
 *
 *    The ref_cnt will always be: FILTERX_OBJECT_REFCOUNT_HIBERNATED
 *
 *    NOTE: hibernated objects are considered "preserved"
 *
 * 4) frozen objects
 *
 *    Frozen objects are preserved for the entire lifecycle of a
 *    filterx-based configuration.  They are allocated when
 *    the config is initialized and freed once the configuration finishes.
 *
 *    The storage and deallocation is taken care of by the freeze() call.
 *    Frozen objects may be deduplicated if they support such operation.
 *
 *    The ref/unref operations are noops.
 *
 *    The ref_cnt will always be: FILTERX_OBJECT_REFCOUNT_FROZEN
 *
 *    NOTE: frozen objects are considered "preserved"
 */
typedef enum _FilterXObjectRefcountRange
{
  FILTERX_OBJECT_REFCOUNT_BARRIER = G_MAXINT32-3,

  /* stack based allocation */
  FILTERX_OBJECT_REFCOUNT_STACK = FILTERX_OBJECT_REFCOUNT_BARRIER,

  /* anything above this point is preserved: either hibernated or frozen */
  FILTERX_OBJECT_REFCOUNT_PRESERVED,

  FILTERX_OBJECT_REFCOUNT_HIBERNATED=FILTERX_OBJECT_REFCOUNT_PRESERVED,
  FILTERX_OBJECT_REFCOUNT_FROZEN,

  __FILTERX_OBJECT_REFCOUNT_MAX
} FilterXObjectRefcountRange;

G_STATIC_ASSERT(__FILTERX_OBJECT_REFCOUNT_MAX == G_MAXINT32);

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
   *     flags             -- to be used by descendant types
   *
   */
  guint readonly:1, weak_referenced:1, is_dirty:1, flags:5;
  FilterXType *type;
};

static inline gboolean
filterx_object_is_ref(FilterXObject *self)
{
  return filterx_type_is_ref(self->type);
}

static inline gboolean
filterx_object_is_cowable(FilterXObject *object)
{
  return filterx_type_is_cowable(object->type);
}

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
  if (type->is_mutable && filterx_object_is_ref(object))
    g_assert("filterx_ref_unwrap() must be used before comparing to mutable types" && FALSE);

  return _filterx_object_is_type(object, type);
}

FilterXObject *filterx_object_getattr_string(FilterXObject *self, const gchar *attr_name);
gboolean filterx_object_setattr_string(FilterXObject *self, const gchar *attr_name, FilterXObject **new_value);

FilterXObject *filterx_object_new(FilterXType *type);
void filterx_object_freeze(FilterXObject **pself);
void filterx_object_unfreeze_and_free(FilterXObject *self);
void filterx_object_hibernate(FilterXObject *self);
void filterx_object_unhibernate_and_free(FilterXObject *self);
void filterx_object_init_instance(FilterXObject *self, FilterXType *type);
void filterx_object_free_method(FilterXObject *self);

void filterx_json_associate_cached_object(struct json_object *jso, FilterXObject *filterx_object);

static inline gboolean
filterx_object_is_preserved(FilterXObject *self)
{
  return g_atomic_counter_get(&self->ref_cnt) >= FILTERX_OBJECT_REFCOUNT_PRESERVED;
}

static inline FilterXObject *
filterx_object_ref(FilterXObject *self)
{
  if (!self)
    return NULL;

  gint r = g_atomic_counter_get(&self->ref_cnt);

  if (G_UNLIKELY(r == FILTERX_OBJECT_REFCOUNT_STACK))
    {
      /* we can't use filterx_object_clone() directly, as that's an inline
       * function declared further below.  Also, filterx_object_clone() does
       * not clone inmutable objects.  We only support allocating inmutable
       * objects on the stack */
      return self->type->clone(self);
    }

  if (r >= FILTERX_OBJECT_REFCOUNT_PRESERVED)
    return self;

  g_assert(r + 1 < FILTERX_OBJECT_REFCOUNT_BARRIER && r > 0);

  g_atomic_counter_inc(&self->ref_cnt);
  return self;
}

static inline void
filterx_object_unref(FilterXObject *self)
{
  if (!self)
    return;

  gint r = g_atomic_counter_get(&self->ref_cnt);
  if (G_UNLIKELY(r == FILTERX_OBJECT_REFCOUNT_STACK))
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

  if (r >= FILTERX_OBJECT_REFCOUNT_PRESERVED)
    return;

  g_assert(r > 0);

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
      msg_error("FilterX: The add method is not supported for the given type",
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


/* NOTE: only use this to validate the type of a potentially ref wrapped
 * object, object is still not going to be compatible with the target type!
 * To be able to cast down to a more specific type, please use
 * filterx_ref_unwrap_*() family of functions */
static inline gboolean
filterx_object_is_type_or_ref(FilterXObject *object, FilterXType *type)
{
  if (filterx_object_is_ref(object))
    return _filterx_object_is_type(((FilterXRef *) object)->value, type);
  return _filterx_object_is_type(object, type);
}

/*
 * Initialize copy-on-write on the specific object.  This function is
 * idempotent and can potentially be called multiple times, subsequent calls
 * will do nothing.
 *
 * Copy-on-write only makes sense for mutable objects.  In case object is
 * not mutable, we just return the original object.
 *
 * Mutable objects are wrapped into a FilterXRef here so that we can track
 * the number of potential writers.  An object that have multiple potential
 * writers will be cloned upon the first write (e.g.  copied on write, aka
 * cow).
 *
 * This function needs to be called after initializing the underlying
 * mutable object and before the first real "fork" happens.  "Fork" here
 * means that the same object is shared between two distinct places, at
 * least as long it is unchanged.
 *
 * NOTE: potentially replaces *pself with a FilterXRef, sinking the passed
 * reference, returning a FilterXRef instead.
 */
static inline void
filterx_object_cow_prepare(FilterXObject **pself)
{
  FilterXObject *value = *pself;
  if (!value || !filterx_object_is_cowable(value))
    return;
  *pself = _filterx_ref_new(value);
}


/* Perform a lazy copy (e.g.  copy-on-write) of @self and return the new
 * copy. This function does nothing for inmutable data types.
 *
 * For copy-on-write to work, all references to @self need to be done
 * through refs, but our @self argument may still point to a bare, but
 * mutable object.
 *
 * In this case, @self is automatically wrapped into a ref here.  This ref
 * is returned using the @pself output argument.  In case @pself is NULL, we
 * assume the caller is not interested, and we either never create a ref to
 * the old copy or we destroy it.
 *
 * The return value is the ref to the new copy.
 */
static inline FilterXObject *
filterx_object_cow_fork2(FilterXObject *self, FilterXObject **pself)
{
  FilterXObject *saved_self = self;
  filterx_object_cow_prepare(&self);

  if (pself)
    {
      *pself = self;
      return filterx_object_clone(self);
    }
  else
    {
      if (self != saved_self)
        return self;

      FilterXObject *result = filterx_object_clone(self);
      filterx_object_unref(self);
      return result;
    }
}

/* Same as filterx_object_cow_fork2() with an input/output argument */
static inline FilterXObject *
filterx_object_cow_fork(FilterXObject **pself)
{
  return filterx_object_cow_fork2(*pself, pself);
}

/* */
static inline FilterXObject *
filterx_object_cow_store(FilterXObject **pself)
{
  filterx_object_cow_prepare(pself);
  return filterx_object_ref(*pself);
}

#endif

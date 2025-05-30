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
#include "filterx-object.h"
#include "filterx-eval.h"
#include "mainloop-worker.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/filterx-globals.h"

FilterXObject *
filterx_object_getattr_string(FilterXObject *self, const gchar *attr_name)
{
  FILTERX_STRING_DECLARE_ON_STACK(attr, attr_name, -1);
  FilterXObject *res = filterx_object_getattr(self, attr);
  filterx_object_unref(attr);
  return res;
}

gboolean
filterx_object_setattr_string(FilterXObject *self, const gchar *attr_name, FilterXObject **new_value)
{
  FILTERX_STRING_DECLARE_ON_STACK(attr, attr_name, -1);
  gboolean res = filterx_object_setattr(self, attr, new_value);
  filterx_object_unref(attr);
  return res;
}

#define INIT_TYPE_METHOD(type, method_name) do { \
    if (type->method_name) \
      break; \
    FilterXType *super_type = type->super_type; \
    while (super_type) \
      { \
        if (super_type->method_name) \
          { \
            type->method_name = super_type->method_name; \
            break; \
          } \
        super_type = super_type->super_type; \
      } \
  } while (0)

void
_filterx_type_init_methods(FilterXType *type)
{
  INIT_TYPE_METHOD(type, unmarshal);
  INIT_TYPE_METHOD(type, marshal);
  INIT_TYPE_METHOD(type, clone);
  INIT_TYPE_METHOD(type, truthy);
  INIT_TYPE_METHOD(type, getattr);
  INIT_TYPE_METHOD(type, setattr);
  INIT_TYPE_METHOD(type, get_subscript);
  INIT_TYPE_METHOD(type, set_subscript);
  INIT_TYPE_METHOD(type, is_key_set);
  INIT_TYPE_METHOD(type, unset_key);
  INIT_TYPE_METHOD(type, list_factory);
  INIT_TYPE_METHOD(type, dict_factory);
  INIT_TYPE_METHOD(type, repr);
  INIT_TYPE_METHOD(type, str);
  INIT_TYPE_METHOD(type, format_json);
  INIT_TYPE_METHOD(type, len);
  INIT_TYPE_METHOD(type, add);
  INIT_TYPE_METHOD(type, free_fn);
}

void
filterx_type_init(FilterXType *type)
{
  _filterx_type_init_methods(type);

  if (!filterx_type_register(type->name, type))
    msg_error("Reregistering filterx type", evt_tag_str("name", type->name));
}

void
filterx_object_free_method(FilterXObject *self)
{
  /* empty */
}

void
filterx_object_init_instance(FilterXObject *self, FilterXType *type)
{
  g_atomic_counter_set(&self->ref_cnt, 1);
  self->type = type;
  self->readonly = !type->is_mutable;
}

FilterXObject *
filterx_object_new(FilterXType *type)
{
  FilterXObject *self = g_new0(FilterXObject, 1);
  filterx_object_init_instance(self, type);
  return self;
}

static void
_filterx_object_preserve(FilterXObject **pself, guint32 new_ref)
{
  FilterXObject *self = *pself;

  /* NOTE: some objects may be weak refd at the point it is frozen (e.g.  a
   * FilterXRef instance with weak_ref towards the root in a dict).  In
   * those cases our ref_cnt will be 2 and not 1.
   *
   * New weak_refs will not be added once frozen.
   */

  gint expected_refs = 1;
  if (self->weak_referenced)
    expected_refs++;

  g_assert(g_atomic_counter_get(&self->ref_cnt) == expected_refs);

  if (self->type->freeze)
    self->type->freeze(pself);

  /* NOTE: type->freeze may change self to replace with a frozen/hibernated
   * version */

  if (self == *pself)
    {
      /* no change in the object, so we are freezing self */
      filterx_object_make_readonly(self);
      g_atomic_counter_set(&self->ref_cnt, new_ref);
      return;
    }

  self = *pself;
  if (g_atomic_counter_get(&self->ref_cnt) >= FILTERX_OBJECT_REFCOUNT_FROZEN)
    {
      /* we get replaced by another frozen (but not hibernated) object */
      g_atomic_counter_inc(&self->ref_cnt);
    }
}

static void
_filterx_object_thaw(FilterXObject *self)
{
  if (self->type->unfreeze)
    self->type->unfreeze(self);

  guint32 ref = 1;
  if (self->weak_referenced)
    ref++;

  g_atomic_counter_set(&self->ref_cnt, ref);
}

/* NOTE: we expect an exclusive reference, as it is not thread safe to be
 * called on the same object from multiple threads */
void
filterx_object_freeze(FilterXObject **pself)
{
  FilterXObject *self = *pself;

  gint r = g_atomic_counter_get(&self->ref_cnt);
  if (r == FILTERX_OBJECT_REFCOUNT_HIBERNATED)
    return;

  if (r >= FILTERX_OBJECT_REFCOUNT_FROZEN)
    {
      g_atomic_counter_inc(&self->ref_cnt);
      return;
    }
  _filterx_object_preserve(pself, FILTERX_OBJECT_REFCOUNT_FROZEN);
}

void
filterx_object_unfreeze_and_free(FilterXObject *self)
{
  if (!self)
    return;
  gint r = g_atomic_counter_get(&self->ref_cnt);
  if (r == FILTERX_OBJECT_REFCOUNT_HIBERNATED)
    return;

  g_assert(r >= FILTERX_OBJECT_REFCOUNT_FROZEN);
  r = g_atomic_counter_exchange_and_add(&self->ref_cnt, -1);
  if (r > FILTERX_OBJECT_REFCOUNT_FROZEN)
    return;

  _filterx_object_thaw(self);
  filterx_object_unref(self);
}

void
filterx_object_hibernate(FilterXObject **pself)
{
  FilterXObject *self = *pself;

  gint r = g_atomic_counter_get(&self->ref_cnt);
  g_assert(r < FILTERX_OBJECT_REFCOUNT_BARRIER);

  _filterx_object_preserve(pself, FILTERX_OBJECT_REFCOUNT_HIBERNATED);
}

void
filterx_object_unhibernate_and_free(FilterXObject *self)
{
  if (!self)
    return;
  gint r = g_atomic_counter_get(&self->ref_cnt);
  g_assert(r == FILTERX_OBJECT_REFCOUNT_HIBERNATED);

  _filterx_object_thaw(self);
  filterx_object_unref(self);
}

FilterXType FILTERX_TYPE_NAME(object) =
{
  .super_type = NULL,
  .name = "object",
  .free_fn = filterx_object_free_method,
};

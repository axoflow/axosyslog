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
  FilterXObject *attr = filterx_string_new(attr_name, -1);
  FilterXObject *res = filterx_object_getattr(self, attr);
  filterx_object_unref(attr);
  return res;
}

gboolean
filterx_object_setattr_string(FilterXObject *self, const gchar *attr_name, FilterXObject **new_value)
{
  FilterXObject *attr = filterx_string_new(attr_name, -1);
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
filterx_type_init(FilterXType *type)
{
  INIT_TYPE_METHOD(type, unmarshal);
  INIT_TYPE_METHOD(type, marshal);
  INIT_TYPE_METHOD(type, clone);
  INIT_TYPE_METHOD(type, map_to_json);
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
  INIT_TYPE_METHOD(type, len);
  INIT_TYPE_METHOD(type, add);
  INIT_TYPE_METHOD(type, free_fn);

  if (!filterx_type_register(type->name, type))
    msg_error("Reregistering filterx type", evt_tag_str("name", type->name));
}

#define FILTERX_OBJECT_MAGIC_BIAS G_MAXINT32

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

gboolean
filterx_object_freeze(FilterXObject *self)
{
  if (filterx_object_is_frozen(self))
    return FALSE;
  g_assert(g_atomic_counter_get(&self->ref_cnt) == 1);
  g_atomic_counter_set(&self->ref_cnt, FILTERX_OBJECT_MAGIC_BIAS);
  return TRUE;
}

gboolean
filterx_object_is_frozen(FilterXObject *self)
{
  return g_atomic_counter_get(&self->ref_cnt) == FILTERX_OBJECT_MAGIC_BIAS;
}

void
filterx_object_unfreeze_and_free(FilterXObject *self)
{
  g_assert(filterx_object_is_frozen(self));
  g_atomic_counter_set(&self->ref_cnt, 1);
  filterx_object_unref(self);
}

FilterXObject *
filterx_object_ref(FilterXObject *self)
{
  if (!self)
    return NULL;

  if (filterx_object_is_frozen(self))
    return self;

  g_atomic_counter_inc(&self->ref_cnt);

  return self;
}

void
filterx_object_unref(FilterXObject *self)
{
  if (!self)
    return;

  if (filterx_object_is_frozen(self))
    return;

  g_assert(g_atomic_counter_get(&self->ref_cnt) > 0);
  if (g_atomic_counter_dec_and_test(&self->ref_cnt))
    {
      self->type->free_fn(self);
      g_free(self);
    }
}

FilterXType FILTERX_TYPE_NAME(object) =
{
  .super_type = NULL,
  .name = "object",
  .free_fn = filterx_object_free_method,
};

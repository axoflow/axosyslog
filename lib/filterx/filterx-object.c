/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "mainloop-worker.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/filterx-globals.h"
#include "filterx/filterx-config.h"

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

void
_filterx_object_log_add_object_error(FilterXObject *self)
{
  filterx_eval_push_error_info_printf("Failed to evaluate addition", NULL,
                                      "The add method is not supported for the given type: %s",
                                      filterx_object_get_type_name(self));
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

FilterXObject *
filterx_object_new(FilterXType *type)
{
  FilterXObject *self = g_new0(FilterXObject, 1);
  filterx_object_init_instance(self, type);
  return self;
}

/* NOTE: we expect an exclusive reference, as it is not thread safe to be
 * called on the same object from multiple threads */
void
filterx_object_freeze(FilterXObject **pself, GlobalConfig *cfg)
{
  FilterXObject *self = *pself;
  FilterXConfig *fx_cfg = filterx_config_get(cfg);

  /* Mutable or recursive objects should never be frozen.
   * Use filterx_object_make_readonly() instead, that is enough to avoid clones.
   */
  g_assert(!filterx_object_is_ref(self) && !self->type->is_mutable);

  if (filterx_object_is_preserved(self))
    return;

  if (filterx_object_dedup(pself, fx_cfg->frozen_deduplicated_objects))
    {
      /* NOTE: filterx_object_dedup() may change self to replace with an already frozen version */
      if (self != *pself)
        return;
    }
  else
    g_ptr_array_add(fx_cfg->frozen_objects, self);

  /* no change in the object, so we are freezing self */
  filterx_object_make_readonly(self);
  g_atomic_counter_set(&self->ref_cnt, FILTERX_OBJECT_REFCOUNT_FROZEN);
}

void
_filterx_object_unfreeze_and_free(FilterXObject *self)
{
  if (!self)
    return;

  gint r = g_atomic_counter_get(&self->ref_cnt);
  if (r == FILTERX_OBJECT_REFCOUNT_HIBERNATED)
    return;

  g_assert(r == FILTERX_OBJECT_REFCOUNT_FROZEN);

  g_atomic_counter_set(&self->ref_cnt, 1);
  filterx_object_unref(self);
}

void
filterx_object_hibernate(FilterXObject *self)
{
  /* do not allow already preserved objects */
  g_assert(!filterx_object_is_preserved(self));

  /* Mutable or recursive objects should never be hibernated.
   * Use filterx_object_make_readonly() instead, that is enough to avoid clones.
   */
  g_assert(!filterx_object_is_ref(self) && !self->type->is_mutable);

  filterx_object_make_readonly(self);
  g_atomic_counter_set(&self->ref_cnt, FILTERX_OBJECT_REFCOUNT_HIBERNATED);
}

void
filterx_object_unhibernate_and_free(FilterXObject *self)
{
  if (!self)
    return;

  gint r = g_atomic_counter_get(&self->ref_cnt);
  g_assert(r == FILTERX_OBJECT_REFCOUNT_HIBERNATED);

  g_atomic_counter_set(&self->ref_cnt, 1);
  filterx_object_unref(self);
}

FilterXType FILTERX_TYPE_NAME(object) =
{
  .super_type = NULL,
  .name = "object",
  .free_fn = filterx_object_free_method,
};

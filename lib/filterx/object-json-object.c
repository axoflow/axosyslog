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

#include "filterx/object-json-internal.h"
#include "filterx/object-extractor.h"
#include "filterx/object-null.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/filterx-weakrefs.h"
#include "filterx/object-dict-interface.h"
#include "filterx/filterx-object-istype.h"
#include "filterx/filterx-ref.h"
#include "syslog-ng.h"
#include "str-utils.h"
#include "logmsg/type-hinting.h"

struct FilterXJsonObject_
{
  FilterXDict super;
  FilterXWeakRef root_container;
  struct json_object *jso;

  GMutex lock;
  const gchar *cached_ro_literal;
};

static gboolean
_truthy(FilterXObject *s)
{
  return TRUE;
}

static inline const gchar *
_json_string(FilterXJsonObject *self)
{
  if (self->super.super.readonly)
    return g_atomic_pointer_get(&self->cached_ro_literal);

  return json_object_to_json_string_ext(self->jso, JSON_C_TO_STRING_PLAIN);
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  *t = LM_VT_JSON;

  const gchar *json_repr = _json_string(self);
  g_string_append(repr, json_repr);

  return TRUE;
}

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  const gchar *json_repr = _json_string(self);
  g_string_append(repr, json_repr);

  return TRUE;
}

static gboolean
_map_to_json(FilterXObject *s, struct json_object **jso, FilterXObject **assoc_object)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  *jso = json_object_get(self->jso);
  return TRUE;
}

static FilterXObject *
_clone(FilterXObject *s)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  struct json_object *jso = filterx_json_deep_copy(self->jso);
  if (!jso)
    return NULL;

  return filterx_json_object_new_sub(jso, NULL);
}

static FilterXObject *
_get_subscript(FilterXDict *s, FilterXObject *key)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  const gchar *key_str;
  gsize len;
  if (!filterx_object_extract_string_ref(key, &key_str, &len))
    return NULL;

  APPEND_ZERO(key_str, key_str, len);

  struct json_object *jso = NULL;
  if (!json_object_object_get_ex(self->jso, key_str, &jso))
    return NULL;

  return filterx_json_convert_json_to_object_cached(&s->super, &self->root_container, jso);
}

static gboolean
_set_subscript(FilterXDict *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  const gchar *key_str;
  gsize len;
  if (!filterx_object_extract_string_ref(key, &key_str, &len))
    return FALSE;

  APPEND_ZERO(key_str, key_str, len);

  struct json_object *jso = NULL;
  FilterXObject *assoc_object = NULL;
  if (!filterx_object_map_to_json(*new_value, &jso, &assoc_object))
    return FALSE;

  filterx_json_associate_cached_object(jso, assoc_object);

  if (json_object_object_add(self->jso, key_str, jso) != 0)
    {
      filterx_object_unref(assoc_object);
      json_object_put(jso);
      return FALSE;
    }

  filterx_object_set_modified_in_place(&self->super.super, TRUE);
  FilterXObject *root_container = filterx_weakref_get(&self->root_container);
  if (root_container)
    {
      filterx_object_set_modified_in_place(root_container, TRUE);
      filterx_object_unref(root_container);
    }

  filterx_object_unref(*new_value);
  *new_value = assoc_object;

  return TRUE;
}

static gboolean
_unset_key(FilterXDict *s, FilterXObject *key)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  const gchar *key_str;
  gsize len;
  if (!filterx_object_extract_string_ref(key, &key_str, &len))
    return FALSE;

  APPEND_ZERO(key_str, key_str, len);

  json_object_object_del(self->jso, key_str);

  filterx_object_set_modified_in_place(&self->super.super, TRUE);
  FilterXObject *root_container = filterx_weakref_get(&self->root_container);
  if (root_container)
    {
      filterx_object_set_modified_in_place(root_container, TRUE);
      filterx_object_unref(root_container);
    }

  return TRUE;
}

static guint64
_len(FilterXDict *s)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  return json_object_object_length(self->jso);
}

static gboolean
_iter_inner(FilterXJsonObject *self, const gchar *obj_key, struct json_object *jso,
            FilterXDictIterFunc func, gpointer user_data)
{
  FILTERX_STRING_DECLARE_ON_STACK(key, obj_key, strlen(obj_key));
  FilterXObject *value = filterx_json_convert_json_to_object_cached(&self->super.super, &self->root_container,
                         jso);

  gboolean result = func(key, value, user_data);

  filterx_object_unref(key);
  filterx_object_unref(value);
  return result;
}

static gboolean
_iter(FilterXDict *s, FilterXDictIterFunc func, gpointer user_data)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  struct json_object_iter itr;
  json_object_object_foreachC(self->jso, itr)
  {
    if (!_iter_inner(self, itr.key, itr.val, func, user_data))
      return FALSE;
  }
  return TRUE;
}

static void
_make_readonly(FilterXObject *s)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  if (g_atomic_pointer_get(&self->cached_ro_literal))
    return;

  /* json_object_to_json_string_ext() writes/caches into jso, so it's not thread safe  */
  g_mutex_lock(&self->lock);
  if (!g_atomic_pointer_get(&self->cached_ro_literal))
    g_atomic_pointer_set(&self->cached_ro_literal, json_object_to_json_string_ext(self->jso, JSON_C_TO_STRING_PLAIN));
  g_mutex_unlock(&self->lock);
}

/* NOTE: consumes root ref */
FilterXObject *
filterx_json_object_new_sub(struct json_object *jso, FilterXObject *root)
{
  FilterXJsonObject *self = g_new0(FilterXJsonObject, 1);
  filterx_dict_init_instance(&self->super, &FILTERX_TYPE_NAME(json_object));

  self->super.get_subscript = _get_subscript;
  self->super.set_subscript = _set_subscript;
  self->super.unset_key = _unset_key;
  self->super.len = _len;
  self->super.iter = _iter;

  filterx_weakref_set(&self->root_container, root);
  filterx_object_unref(root);
  self->jso = jso;

  g_mutex_init(&self->lock);

  return &self->super.super;
}

static void
_free(FilterXObject *s)
{
  FilterXJsonObject *self = (FilterXJsonObject *) s;

  json_object_put(self->jso);
  filterx_weakref_clear(&self->root_container);

  g_mutex_clear(&self->lock);

  filterx_object_free_method(s);
}

FilterXObject *
filterx_json_object_new_from_repr(const gchar *repr, gssize repr_len)
{
  struct json_object *jso;
  if (!type_cast_to_json(repr, repr_len, &jso, NULL))
    return NULL;

  return filterx_json_object_new_sub(jso, NULL);
}

FilterXObject *
filterx_json_object_new_empty(void)
{
  return filterx_json_object_new_sub(json_object_new_object(), NULL);
}

const gchar *
filterx_json_object_to_json_literal(FilterXObject *s)
{
  s = filterx_ref_unwrap_ro(s);
  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(json_object)))
    return NULL;

  FilterXJsonObject *self = (FilterXJsonObject *) s;
  return _json_string(self);
}

/* NOTE: Consider using filterx_object_extract_json_object() to also support message_value. */
struct json_object *
filterx_json_object_get_value(FilterXObject *s)
{
  s = filterx_ref_unwrap_ro(s);
  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(json_object)))
    return NULL;

  FilterXJsonObject *self = (FilterXJsonObject *) s;
  return self->jso;
}

static FilterXObject *
_list_factory(FilterXObject *self)
{
  return filterx_json_array_new_empty();
}

static FilterXObject *
_dict_factory(FilterXObject *self)
{
  return filterx_json_object_new_empty();
}

FILTERX_DEFINE_TYPE(json_object, FILTERX_TYPE_NAME(dict),
                    .is_mutable = TRUE,
                    .truthy = _truthy,
                    .free_fn = _free,
                    .marshal = _marshal,
                    .repr = _repr,
                    .map_to_json = _map_to_json,
                    .clone = _clone,
                    .list_factory = _list_factory,
                    .dict_factory = _dict_factory,
                    .make_readonly = _make_readonly,
                   );

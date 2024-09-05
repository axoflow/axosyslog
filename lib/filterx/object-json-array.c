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
#include "filterx/object-message-value.h"
#include "filterx/filterx-weakrefs.h"
#include "filterx/object-list-interface.h"
#include "filterx/expr-function.h"
#include "filterx/filterx-eval.h"

#include "logmsg/type-hinting.h"
#include "str-repr/encode.h"

#define JSON_ARRAY_MAX_SIZE 65536

struct FilterXJsonArray_
{
  FilterXList super;
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
_json_string(FilterXJsonArray *self)
{
  if (self->super.super.readonly)
    return g_atomic_pointer_get(&self->cached_ro_literal);

  return json_object_to_json_string_ext(self->jso, JSON_C_TO_STRING_PLAIN);
}

static gboolean
_marshal_to_json_literal_append(FilterXJsonArray *self, GString *repr, LogMessageValueType *t)
{
  *t = LM_VT_JSON;

  const gchar *json_repr = _json_string(self);
  g_string_append(repr, json_repr);

  return TRUE;
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  for (gint i = 0; i < json_object_array_length(self->jso); i++)
    {
      struct json_object *el = json_object_array_get_idx(self->jso, i);
      if (json_object_get_type(el) != json_type_string)
        return _marshal_to_json_literal_append(self, repr, t);

      if (i != 0)
        g_string_append_c(repr, ',');

      str_repr_encode_append(repr, json_object_get_string(el), -1, NULL);
    }

  *t = LM_VT_LIST;
  return TRUE;
}

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  const gchar *json_repr = _json_string(self);
  g_string_append(repr, json_repr);

  return TRUE;
}

static gboolean
_map_to_json(FilterXObject *s, struct json_object **jso, FilterXObject **assoc_object)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  *jso = json_object_get(self->jso);
  return TRUE;
}

static FilterXObject *
_clone(FilterXObject *s)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  struct json_object *jso = filterx_json_deep_copy(self->jso);
  if (!jso)
    return NULL;

  return filterx_json_array_new_sub(jso, NULL);
}

static FilterXObject *
_get_subscript(FilterXList *s, guint64 index)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  struct json_object *jso = json_object_array_get_idx(self->jso, index);

  return filterx_json_convert_json_to_object_cached(&s->super, &self->root_container, jso);
}

static guint64
_len(FilterXList *s)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  return json_object_array_length(self->jso);
}

static gboolean
_append(FilterXList *s, FilterXObject **new_value)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  if (G_UNLIKELY(_len(s) >= JSON_ARRAY_MAX_SIZE))
    return FALSE;

  struct json_object *jso = NULL;
  FilterXObject *assoc_object = NULL;
  if (!filterx_object_map_to_json(*new_value, &jso, &assoc_object))
    return FALSE;

  filterx_json_associate_cached_object(jso, assoc_object);

  if (json_object_array_add(self->jso, jso) != 0)
    {
      filterx_object_unref(assoc_object);
      json_object_put(jso);
      return FALSE;
    }

  self->super.super.modified_in_place = TRUE;
  FilterXObject *root_container = filterx_weakref_get(&self->root_container);
  if (root_container)
    {
      root_container->modified_in_place = TRUE;
      filterx_object_unref(root_container);
    }

  filterx_object_unref(*new_value);
  *new_value = assoc_object;

  return TRUE;
}

static gboolean
_set_subscript(FilterXList *s, guint64 index, FilterXObject **new_value)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  if (G_UNLIKELY(index >= JSON_ARRAY_MAX_SIZE))
    return FALSE;

  struct json_object *jso = NULL;
  FilterXObject *assoc_object = NULL;
  if (!filterx_object_map_to_json(*new_value, &jso, &assoc_object))
    return FALSE;

  filterx_json_associate_cached_object(jso, assoc_object);

  if (json_object_array_put_idx(self->jso, index, jso) != 0)
    {
      filterx_object_unref(assoc_object);
      json_object_put(jso);
      return FALSE;
    }

  self->super.super.modified_in_place = TRUE;
  FilterXObject *root_container = filterx_weakref_get(&self->root_container);
  if (root_container)
    {
      root_container->modified_in_place = TRUE;
      filterx_object_unref(root_container);
    }

  filterx_object_unref(*new_value);
  *new_value = assoc_object;

  return TRUE;
}

static gboolean
_unset_index(FilterXList *s, guint64 index)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  if (G_UNLIKELY(index >= JSON_ARRAY_MAX_SIZE))
    return FALSE;

  if (json_object_array_del_idx(self->jso, index, 1) != 0)
    return FALSE;

  self->super.super.modified_in_place = TRUE;
  FilterXObject *root_container = filterx_weakref_get(&self->root_container);
  if (root_container)
    {
      root_container->modified_in_place = TRUE;
      filterx_object_unref(root_container);
    }

  return TRUE;
}

static void
_make_readonly(FilterXObject *s)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

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
filterx_json_array_new_sub(struct json_object *jso, FilterXObject *root)
{
  FilterXJsonArray *self = g_new0(FilterXJsonArray, 1);
  filterx_list_init_instance(&self->super, &FILTERX_TYPE_NAME(json_array));

  self->super.super.make_readonly = _make_readonly;
  self->super.get_subscript = _get_subscript;
  self->super.set_subscript = _set_subscript;
  self->super.append = _append;
  self->super.unset_index = _unset_index;
  self->super.len = _len;

  filterx_weakref_set(&self->root_container, root);
  filterx_object_unref(root);
  self->jso = jso;

  g_mutex_init(&self->lock);

  return &self->super.super;
}

static void
_free(FilterXObject *s)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  json_object_put(self->jso);
  filterx_weakref_clear(&self->root_container);

  g_mutex_clear(&self->lock);
}

FilterXObject *
filterx_json_array_new_from_repr(const gchar *repr, gssize repr_len)
{
  struct json_object *jso;
  if (!type_cast_to_json(repr, repr_len, &jso, NULL))
    return NULL;

  if (!json_object_is_type(jso, json_type_array))
    {
      json_object_put(jso);
      return NULL;
    }

  return filterx_json_array_new_sub(jso, NULL);
}

FilterXObject *
filterx_json_array_new_from_syslog_ng_list(const gchar *repr, gssize repr_len)
{
  struct json_object *jso;
  if (!type_cast_to_json_from_list(repr, repr_len, &jso, NULL))
    return NULL;

  return filterx_json_array_new_sub(jso, NULL);
}

FilterXObject *
filterx_json_array_new_from_args(FilterXExpr *s, GPtrArray *args)
{
  if (!args || args->len == 0)
    return filterx_json_array_new_empty();

  if (args->len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires zero or one argument", FALSE);
      return NULL;
    }

  FilterXObject *arg = (FilterXObject *) g_ptr_array_index(args, 0);

  if (filterx_object_is_type(arg, &FILTERX_TYPE_NAME(json_array)))
    return filterx_object_ref(arg);

  struct json_object *jso;
  if (filterx_object_extract_json_array(arg, &jso))
    return filterx_json_array_new_sub(jso, NULL);

  const gchar *repr;
  gsize repr_len;
  if (filterx_object_extract_string(arg, &repr, &repr_len))
    return filterx_json_array_new_from_repr(repr, repr_len);

  filterx_eval_push_error_info("Argument must be a json array, a string or a syslog-ng list", s,
                               g_strdup_printf("got \"%s\" instead", arg->type->name), TRUE);
  return NULL;
}

FilterXObject *
filterx_json_array_new_empty(void)
{
  return filterx_json_array_new_sub(json_object_new_array(), NULL);
}

const gchar *
filterx_json_array_to_json_literal(FilterXObject *s)
{
  FilterXJsonArray *self = (FilterXJsonArray *) s;

  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(json_array)))
    return NULL;
  return _json_string(self);
}

/* NOTE: Consider using filterx_object_extract_json_array() to also support message_value. */
struct json_object *
filterx_json_array_get_value(FilterXObject *s)
{
  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(json_array)))
    return NULL;

  FilterXJsonArray *self = (FilterXJsonArray *) s;

  return self->jso;
}

FILTERX_DEFINE_TYPE(json_array, FILTERX_TYPE_NAME(list),
                    .is_mutable = TRUE,
                    .truthy = _truthy,
                    .free_fn = _free,
                    .marshal = _marshal,
                    .repr = _repr,
                    .map_to_json = _map_to_json,
                    .clone = _clone,
                    .list_factory = filterx_json_array_new_empty,
                    .dict_factory = filterx_json_object_new_empty,
                   );

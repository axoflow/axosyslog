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
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-message-value.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object-istype.h"
#include "filterx/filterx-ref.h"

#include "scanner/list-scanner/list-scanner.h"
#include "str-repr/encode.h"

static gboolean
_is_json_cacheable(struct json_object *jso)
{
  if (json_object_get_type(jso) == json_type_double && JSON_C_MAJOR_VERSION == 0 && JSON_C_MINOR_VERSION < 14)
    {
      /*
       * See: filterx_json_get_cached_object()
       *
       * The workaround only works starting with 0.14, before that json_object_set_double()
       * does not drop the string user data, so we cannot check if it is our
       * filterx object or the string, which means we cannot cache doubles.
       */
      return FALSE;
    }

  return TRUE;
}

static int
_deep_copy_filterx_object_ref(json_object *src, json_object *parent, const char *key, size_t index, json_object **dst)
{
  int result = json_c_shallow_copy_default(src, parent, key, index, dst);
  if (*dst == NULL)
    return result;

  /* we need to copy the userdata for primitive types */

  switch (json_object_get_type(src))
    {
    case json_type_null:
    case json_type_boolean:
    case json_type_double:
    case json_type_int:
    case json_type_string:
    {
      FilterXObject *fobj = filterx_json_get_cached_object(src);
      if (fobj)
        filterx_json_associate_cached_object(*dst, fobj);
      break;
    }
    default:
      break;
    }
  return 2;
}

struct json_object *
filterx_json_deep_copy(struct json_object *jso)
{
  struct json_object *clone = NULL;
  if (json_object_deep_copy(jso, &clone, _deep_copy_filterx_object_ref) != 0)
    return NULL;

  return clone;
}

FilterXObject *
filterx_json_convert_json_to_object(FilterXObject *root_obj, FilterXWeakRef *root_container, struct json_object *jso)
{
  switch (json_object_get_type(jso))
    {
    case json_type_null:
      return filterx_null_new();
    case json_type_double:
      return filterx_double_new(json_object_get_double(jso));
    case json_type_boolean:
      return filterx_boolean_new(json_object_get_boolean(jso));
    case json_type_int:
      return filterx_integer_new(json_object_get_int64(jso));
    case json_type_string:
      return filterx_string_new(json_object_get_string(jso), json_object_get_string_len(jso));
    case json_type_array:
      return filterx_json_array_new_sub(json_object_get(jso),
                                        filterx_weakref_get(root_container) ? : filterx_object_ref(root_obj));
    case json_type_object:
      return filterx_json_object_new_sub(json_object_get(jso),
                                         filterx_weakref_get(root_container) ? : filterx_object_ref(root_obj));
    default:
      g_assert_not_reached();
    }
}

FilterXObject *
filterx_json_convert_json_to_object_cached(FilterXObject *self, FilterXWeakRef *root_container,
                                           struct json_object *jso)
{
  FilterXObject *filterx_obj;

  if (!jso || json_object_get_type(jso) == json_type_null)
    return filterx_null_new();

  /* NOTE: userdata is a weak reference */
  filterx_obj = filterx_json_get_cached_object(jso);
  if (filterx_obj)
    return filterx_object_ref(filterx_obj);

  filterx_obj = filterx_json_convert_json_to_object(self, root_container, jso);
  filterx_json_associate_cached_object(jso, filterx_obj);
  return filterx_obj;
}

void
filterx_json_associate_cached_object(struct json_object *jso, FilterXObject *filterx_obj)
{
  /* null JSON value turns into NULL pointer. */
  if (!jso)
    return;

  if (!_is_json_cacheable(jso))
    return;

  filterx_eval_store_weak_ref(filterx_obj);

  /* we are not storing a reference in userdata to avoid circular
   * references.  That ref is retained by the filterx_scope_store_weak_ref()
   * call above, which is automatically terminated when the scope ends */
  json_object_set_userdata(jso, filterx_obj, NULL);
}

FilterXObject *
filterx_json_get_cached_object(struct json_object *jso)
{
  if (!_is_json_cacheable(jso))
    return NULL;

  if (json_object_get_type(jso) == json_type_double)
    {
      /*
       * This is a workaround to ditch builtin serializer for double objects
       * that are set when parsing from a string representation.
       * json_object_double_new_ds() will set userdata to the string
       * representation of the number, as extracted from the JSON source.
       *
       * Changing the value of the double to the same value, ditches this,
       * but only if necessary.
       */
      json_object_set_double(jso, json_object_get_double(jso));
    }

  return (FilterXObject *) json_object_get_userdata(jso);
}

FilterXObject *
filterx_json_new_from_repr(const gchar *repr, gssize repr_len)
{
  if (repr_len == 0 || repr[0] == '{')
    return filterx_json_object_new_from_repr(repr, repr_len);
  return filterx_json_array_new_from_repr(repr, repr_len);
}

FilterXObject *
filterx_json_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (!args || args_len == 0)
    return filterx_json_object_new_empty();

  if (args_len != 1)
    {
      filterx_eval_push_error("Too many arguments", s, NULL);
      return NULL;
    }

  FilterXObject *arg = args[0];

  FilterXObject *arg_unwrapped = filterx_ref_unwrap_ro(arg);
  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(json_array)) ||
      filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(json_object)))
    return filterx_object_ref(arg);

  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(dict)))
    {
      FilterXObject *self = filterx_json_object_new_empty();
      if (!filterx_dict_merge(self, arg_unwrapped))
        {
          filterx_object_unref(self);
          return NULL;
        }
      return self;
    }

  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(list)))
    {
      FilterXObject *self = filterx_json_array_new_empty();
      if (!filterx_list_merge(self, arg_unwrapped))
        {
          filterx_object_unref(self);
          return NULL;
        }
      return self;
    }

  struct json_object *jso;
  if (filterx_object_extract_json_object(arg, &jso))
    return filterx_json_new_from_object(jso);

  const gchar *repr;
  gsize repr_len;
  if (filterx_object_extract_string_ref(arg, &repr, &repr_len))
    return filterx_json_new_from_repr(repr, repr_len);

  filterx_eval_push_error_info("Argument must be a json, a string or a syslog-ng list", s,
                               g_strdup_printf("got \"%s\" instead", arg_unwrapped->type->name), TRUE);
  return NULL;
}

FilterXObject *
filterx_json_new_from_object(struct json_object *jso)
{
  if (json_object_get_type(jso) == json_type_object)
    return filterx_json_object_new_sub(jso, NULL);

  if (json_object_get_type(jso) == json_type_array)
    return filterx_json_array_new_sub(jso, NULL);

  return NULL;
}

const gchar *
filterx_json_to_json_literal(FilterXObject *s)
{
  s = filterx_ref_unwrap_ro(s);

  if (filterx_object_is_type(s, &FILTERX_TYPE_NAME(json_object)))
    return filterx_json_object_to_json_literal(s);
  if (filterx_object_is_type(s, &FILTERX_TYPE_NAME(json_array)))
    return filterx_json_array_to_json_literal(s);
  return NULL;
}

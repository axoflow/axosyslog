/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/object-tuple.h"
#include "filterx/object-string.h"
#include "filterx/json-repr.h"
#include "filterx/filterx-eval.h"

typedef gboolean (*FilterXTupleForeachFunc)(gsize index, FilterXObject **, gpointer);

static gboolean
filterx_tuple_foreach(FilterXTuple *self, FilterXTupleForeachFunc func, gpointer user_data)
{
  for (gsize i = 0; i < self->array->len; i++)
    {
      FilterXObject **el = (FilterXObject **) &g_ptr_array_index(self->array, i);
      if (!func(i, el, user_data))
        return FALSE;
    }
  return TRUE;
}

static gboolean
_filterx_tuple_truthy(FilterXObject *s)
{
  return TRUE;
}

static gboolean
_filterx_tuple_repr(FilterXObject *s, GString *repr)
{
  FilterXTuple *self = (FilterXTuple *) s;
  g_string_append_c(repr, '(');

  for (gsize i = 0; i < self->array->len; i++)
    {
      FilterXObject *elt = (FilterXObject *) g_ptr_array_index(self->array, i);
      if (!filterx_object_repr_append(elt, repr))
        return FALSE;
      if (i == 0 || i < self->array->len - 1)
        g_string_append_c(repr, ',');
    }

  g_string_append_c(repr, ')');
  return TRUE;
}

static gboolean
_filterx_tuple_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  if (!filterx_object_to_json(s, repr))
    return FALSE;
  *t = LM_VT_JSON;
  return TRUE;
}

static FilterXObject *
_filterx_tuple_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXTuple *self = (FilterXTuple *) s;

  guint64 normalized_index;
  const gchar *error;
  if (!filterx_sequence_normalize_index(key, self->array->len, &normalized_index, FALSE, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return NULL;
    }

  return filterx_object_ref(g_ptr_array_index(self->array, normalized_index));
}

static gboolean
_filterx_tuple_len(FilterXObject *s, guint64 *len)
{
  FilterXTuple *self = (FilterXTuple *) s;

  *len = self->array->len;
  return TRUE;
}

static gboolean
_filterx_tuple_is_key_set(FilterXObject *s, FilterXObject *key)
{
  FilterXTuple *self = (FilterXTuple *) s;

  guint64 normalized_index;
  const gchar *error;
  return filterx_sequence_normalize_index(key, self->array->len, &normalized_index, FALSE, &error);
}

static gboolean
_filterx_tuple_iter(FilterXObject *s, FilterXObjectIterFunc func, gpointer user_data)
{
  FilterXTuple *self = (FilterXTuple *) s;

  for (gsize i = 0; i < self->array->len; i++)
    {
      FilterXObject *value = g_ptr_array_index(self->array, i);
      FILTERX_INTEGER_DECLARE_ON_STACK(index_obj, i);
      gboolean result = func(index_obj, value, user_data);
      FILTERX_INTEGER_CLEAR_FROM_STACK(index_obj);
      if (!result)
        return FALSE;
    }
  return TRUE;
}

static gboolean
_filterx_tuple_equal(FilterXObject *s, FilterXObject *o)
{
  FilterXTuple *self = (FilterXTuple *) s;
  FilterXTuple *other = (FilterXTuple *) o;

  if (self->array->len != other->array->len)
    return FALSE;

  for (gsize i = 0; i < self->array->len; i++)
    {
      FilterXObject *v1 = g_ptr_array_index(self->array, i);
      FilterXObject *v2 = g_ptr_array_index(other->array, i);
      if (!filterx_object_equal(v1, v2))
        return FALSE;
    }
  return TRUE;
}


/*
 * This is a slightly simplified version of the xxHash non-cryptographic hash:
 *
 * For the xxHash specification, see
 * https://github.com/Cyan4973/xxHash/blob/master/doc/xxhash_spec.md
 */

#define _TUPLE_HASH_XXPRIME_1 ((guint)2654435761UL)
#define _TUPLE_HASH_XXPRIME_2 ((guint)2246822519UL)
#define _TUPLE_HASH_XXPRIME_5 ((guint)374761393UL)
#define _TUPLE_HASH_XXROTATE(x) ((x << 13) | (x >> 19))  /* Rotate left 13 bits */

static guint
_filterx_tuple_hash(FilterXObject *s)
{
  FilterXTuple *self = (FilterXTuple *) s;

  guint acc = _TUPLE_HASH_XXPRIME_5;
  gsize len = self->array->len;

  for (gsize i = 0; i < len; i++)
    {
      FilterXObject *value = g_ptr_array_index(self->array, i);
      guint hash = filterx_object_hash(value);
      acc += hash * _TUPLE_HASH_XXPRIME_2;
      acc = _TUPLE_HASH_XXROTATE(acc);
      acc *= _TUPLE_HASH_XXPRIME_1;
    }
  acc += len ^ _TUPLE_HASH_XXPRIME_5;
  return acc;
}

static FilterXObject *
_filterx_tuple_clone(FilterXObject *s)
{
  FilterXTuple *self = (FilterXTuple *) s;
  FilterXTuple *clone = (FilterXTuple *) filterx_tuple_new(self->array->len);

  for (gsize i = 0; i < self->array->len; i++)
    {
      FilterXObject *el = g_ptr_array_index(self->array, i);

      el = filterx_object_copy(el);
      g_ptr_array_add(clone->array, el);
    }
  return &clone->super.super;
}

static gboolean
_dedup_tuple_item(gsize index, FilterXObject **value, gpointer user_data)
{
  FilterXObjectDeduplicator *dedup = (FilterXObjectDeduplicator *) user_data;

  filterx_object_dedup(value, dedup);

  return TRUE;
}

static void
_filterx_tuple_dedup(FilterXObject **pself, FilterXObjectDeduplicator *dedup)
{
  FilterXTuple *self = (FilterXTuple *) *pself;

  g_assert(filterx_tuple_foreach(self, _dedup_tuple_item, dedup));
  g_assert(*pself == &self->super.super);
}

static gboolean
_freeze_tuple_item(gsize index, FilterXObject **value, gpointer user_data)
{
  FilterXObjectFreezer *freezer = (FilterXObjectFreezer *) user_data;

  filterx_object_freeze(*value, freezer);

  return TRUE;
}

static void
_filterx_tuple_freeze(FilterXObject *s, FilterXObjectFreezer *freezer)
{
  FilterXTuple *self = (FilterXTuple *) s;

  filterx_object_freezer_keep(freezer, s);
  g_assert(filterx_tuple_foreach(self, _freeze_tuple_item, freezer));
}

static void
_filterx_tuple_free(FilterXObject *s)
{
  FilterXTuple *self = (FilterXTuple *) s;

  g_ptr_array_unref(self->array);
  filterx_object_free_method(s);
}

FilterXObject *
filterx_tuple_new(gsize len)
{
  FilterXTuple *self = filterx_new_object(FilterXTuple);
  filterx_sequence_init_instance(&self->super, &FILTERX_TYPE_NAME(tuple));

  self->array = g_ptr_array_new_full(len, (GDestroyNotify) filterx_object_unref);
  return &self->super.super;
}

FilterXObject *
filterx_tuple_new_from_sequence(FilterXObject *sequence)
{
  guint64 len;

  g_assert(filterx_object_len(sequence, &len));

  FilterXTuple *self = (FilterXTuple *) filterx_tuple_new(len);
  for (guint64 i = 0; i < len; i++)
    {
      FilterXObject *elt = filterx_sequence_get_subscript(sequence, i);
      g_ptr_array_add(self->array, filterx_object_copy(elt));
      filterx_object_unref(elt);
    }

  return &self->super.super;
}

FilterXObject *
filterx_tuple_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args_len == 0)
    return filterx_tuple_new(0);

  if (args_len != 1)
    {
      filterx_eval_push_error("Too many arguments", s, NULL);
      return NULL;
    }

  FilterXObject *arg = args[0];
  FilterXObject *arg_unwrapped = filterx_ref_unwrap_ro(arg);
  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(tuple)))
    return filterx_object_ref(arg);

  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(sequence)))
    {
      return filterx_tuple_new_from_sequence(arg_unwrapped);
    }

  const gchar *repr;
  gsize repr_len;
  repr = filterx_string_get_value_ref(arg, &repr_len);
  if (repr)
    {
      GError *error = NULL;
      FilterXObject *jso = filterx_object_from_json(repr, repr_len, &error);
      if (!jso)
        {
          filterx_eval_push_error_info_printf("Failed to create tuple", s,
                                              "Argument must be a valid JSON string: %s",
                                              error->message);
          g_clear_error(&error);
          return NULL;
        }
      FilterXObject *result = filterx_tuple_new_from_args(s, &jso, 1);
      filterx_object_unref(jso);
      return result;
    }

  filterx_eval_push_error_info_printf("Failed to create tuple", s,
                                      "Argument must be a sequence or a string, got: %s",
                                      filterx_object_get_type_name(arg));
  return NULL;
}

FILTERX_DEFINE_TYPE(tuple, FILTERX_TYPE_NAME(sequence),
                    .truthy = _filterx_tuple_truthy,
                    .free_fn = _filterx_tuple_free,
                    .marshal = _filterx_tuple_marshal,
                    .repr = _filterx_tuple_repr,
                    .clone = _filterx_tuple_clone,
                    .get_subscript = _filterx_tuple_get_subscript,
                    .is_key_set = _filterx_tuple_is_key_set,
                    .iter = _filterx_tuple_iter,
                    .equal = _filterx_tuple_equal,
                    .hash = _filterx_tuple_hash,
                    .freeze = _filterx_tuple_freeze,
                    .dedup = _filterx_tuple_dedup,
                    .len = _filterx_tuple_len,
                   );

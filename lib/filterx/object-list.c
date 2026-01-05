/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/object-list.h"
#include "filterx/filterx-sequence.h"
#include "filterx/object-dict.h"
#include "filterx/filterx-mapping.h"
#include "filterx/json-repr.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-extractor.h"
#include "filterx/json-repr.h"
#include "logmsg/type-hinting.h"
#include "utf8utils.h"
#include "str-format.h"
#include "str-utils.h"
#include "scratch-buffers.h"
#include "str-repr/encode.h"
#include "scanner/list-scanner/list-scanner.h"

typedef gboolean (*FilterXListForeachFunc)(gsize index, FilterXObject **, gpointer);

static gboolean
filterx_list_foreach(FilterXListObject *self, FilterXListForeachFunc func, gpointer user_data)
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
_filterx_list_truthy(FilterXObject *s)
{
  return TRUE;
}

static gboolean
_filterx_list_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXListObject *self = (FilterXListObject *) s;
  gsize initial_len = repr->len;

  for (gint i = 0; i < self->array->len; i++)
    {
      FilterXObject *el = (FilterXObject *) g_ptr_array_index(self->array, i);

      if (!filterx_object_is_type(el, &FILTERX_TYPE_NAME(string)))
        {
          /* ok, we have a non-string element, marshal this as JSON */
          g_string_truncate(repr, initial_len);
          if (!filterx_object_to_json(s, repr))
            return FALSE;
          *t = LM_VT_JSON;
          return TRUE;
        }

      if (i != 0)
        g_string_append_c(repr, ',');

      const gchar *str;
      gsize str_len;
      str = filterx_string_get_value_ref(el, &str_len);
      if (!str)
        g_assert_not_reached();
      str_repr_encode_append(repr, str, str_len, NULL);
    }

  *t = LM_VT_LIST;
  return TRUE;
}

static gboolean
_filterx_list_repr(FilterXObject *s, GString *repr)
{
  return filterx_object_to_json(s, repr);
}

static FilterXObject *
_filterx_list_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXListObject *self = (FilterXListObject *) s;

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
_filterx_list_set_subscript(FilterXObject *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXListObject *self = (FilterXListObject *) s;

  guint64 normalized_index;
  const gchar *error;
  if (!filterx_sequence_normalize_index(key, self->array->len, &normalized_index, TRUE, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return FALSE;
    }

  if (normalized_index >= self->array->len)
    g_ptr_array_set_size(self->array, normalized_index + 1);

  FilterXObject **slot = (FilterXObject **) &g_ptr_array_index(self->array, normalized_index);
  filterx_ref_unset_parent_container(*slot);
  filterx_object_unref(*slot);
  *slot = filterx_object_cow_store(new_value);
  return TRUE;
}

static gboolean
_filterx_list_unset_key(FilterXObject *s, FilterXObject *key)
{
  FilterXListObject *self = (FilterXListObject *) s;

  guint64 normalized_index;
  const gchar *error;
  if (!filterx_sequence_normalize_index(key, self->array->len, &normalized_index, FALSE, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return FALSE;
    }

  FilterXObject *v = (FilterXObject *) g_ptr_array_index(self->array, normalized_index);
  filterx_ref_unset_parent_container(v);
  g_ptr_array_remove_index(self->array, normalized_index);
  return TRUE;
}

static gboolean
_filterx_list_len(FilterXObject *s, guint64 *len)
{
  FilterXListObject *self = (FilterXListObject *) s;

  *len = self->array->len;
  return TRUE;
}

static gboolean
_filterx_list_is_key_set(FilterXObject *s, FilterXObject *key)
{
  FilterXListObject *self = (FilterXListObject *) s;

  guint64 normalized_index;
  const gchar *error;
  return filterx_sequence_normalize_index(key, self->array->len, &normalized_index, FALSE, &error);
}

FilterXObject *
filterx_list_new(void)
{
  FilterXListObject *self = filterx_new_object(FilterXListObject);
  filterx_sequence_init_instance(&self->super, &FILTERX_TYPE_NAME(list));

  self->array = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_object_unref);
  return &self->super.super;
}

static FilterXObject *
_filterx_list_clone_container(FilterXObject *s, FilterXObject *container, FilterXObject *child_of_interest)
{
  FilterXListObject *self = (FilterXListObject *) s;
  FilterXListObject *clone = (FilterXListObject *) filterx_list_new();
  gboolean child_found = FALSE;

  for (gsize i = 0; i < self->array->len; i++)
    {
      FilterXObject *el = g_ptr_array_index(self->array, i);

      if (child_of_interest && filterx_ref_values_equal(el, child_of_interest))
        {
          /* child_of_interest is a movable, floating xref, which is grounded by this clone */
          el = filterx_object_clone(child_of_interest);
#if SYSLOG_NG_ENABLE_DEBUG
          g_assert(el == child_of_interest);
#endif
          child_found = TRUE;
        }
      else
        el = filterx_object_clone(el);
      filterx_ref_set_parent_container(el, container);
      g_ptr_array_add(clone->array, el);
    }
  g_assert(child_found || child_of_interest == NULL);
  return &clone->super.super;
}

static gboolean
_filterx_list_iter(FilterXObject *s, FilterXObjectIterFunc func, gpointer user_data)
{
  FilterXListObject *self = (FilterXListObject *) s;

  for (gsize i = 0; i < self->array->len; i++)
    {
      FilterXObject *value = g_ptr_array_index(self->array, i);
      FILTERX_INTEGER_DECLARE_ON_STACK(index_obj, i);
      func(index_obj, value, user_data);
      FILTERX_INTEGER_CLEAR_FROM_STACK(index_obj);
    }
  return TRUE;
}

static FilterXObject *
_filterx_list_clone(FilterXObject *s)
{
  return _filterx_list_clone_container(s, NULL, NULL);
}

static gboolean
_freeze_list_item(gsize index, FilterXObject **value, gpointer user_data)
{
  FilterXObjectFreezer *freezer = (FilterXObjectFreezer *) user_data;

  filterx_object_freeze(value, freezer);

  return TRUE;
}

static void
_filterx_list_freeze(FilterXObject **pself, FilterXObjectFreezer *freezer)
{
  FilterXListObject *self = (FilterXListObject *) *pself;

  filterx_object_freezer_keep(freezer, *pself);
  g_assert(filterx_list_foreach(self, _freeze_list_item, freezer));

  /* Mutable objects themselves should never be deduplicated,
   * only immutable values INSIDE those recursive mutable objects.
   */
  g_assert(*pself == &self->super.super);
}

static gboolean
_readonly_list_item(gsize index, FilterXObject **value, gpointer user_data)
{
  filterx_object_make_readonly(*value);
  return TRUE;
}

static void
_filterx_list_make_readonly(FilterXObject *s)
{
  FilterXListObject *self = (FilterXListObject *) s;
  filterx_list_foreach(self, _readonly_list_item, NULL);
}

static void
_filterx_list_free(FilterXObject *s)
{
  FilterXListObject *self = (FilterXListObject *) s;

  g_ptr_array_unref(self->array);
  filterx_object_free_method(s);
}

FilterXObject *
filterx_list_new_from_syslog_ng_list(const gchar *repr, gssize repr_len)
{
  FilterXObject *list = filterx_list_new();
  ListScanner scanner;
  list_scanner_init(&scanner);
  list_scanner_input_string(&scanner, repr, repr_len);
  for (gint i = 0; list_scanner_scan_next(&scanner); i++)
    {
      FILTERX_STRING_DECLARE_ON_STACK(value,
                                      list_scanner_get_current_value(&scanner),
                                      list_scanner_get_current_value_len(&scanner));
      gboolean success = filterx_sequence_set_subscript(list, i, &value);
      FILTERX_STRING_CLEAR_FROM_STACK(value);

      if (!success)
        {
          filterx_object_unref(list);
          list = NULL;
          break;
        }
    }
  list_scanner_deinit(&scanner);
  return list;
}

FilterXObject *
filterx_list_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args_len == 0)
    return filterx_list_new();

  if (args_len != 1)
    {
      filterx_eval_push_error("Too many arguments", s, NULL);
      return NULL;
    }

  FilterXObject *arg = args[0];

  FilterXObject *arg_unwrapped = filterx_ref_unwrap_ro(arg);
  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(list)))
    return filterx_object_ref(arg);

  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(sequence)))
    {
      FilterXObject *self = filterx_list_new();
      if (!filterx_sequence_merge(self, arg_unwrapped))
        {
          filterx_object_unref(self);
          return NULL;
        }
      return self;
    }

  const gchar *repr;
  gsize repr_len;
  repr = filterx_string_get_value_ref(arg, &repr_len);
  if (repr)
    {
      GError *error = NULL;
      FilterXObject *self = filterx_object_from_json(repr, repr_len, &error);
      if (!self)
        {
          filterx_eval_push_error_info_printf("Failed to create list", s,
                                              "Argument must be a valid JSON string: %s",
                                              error->message);
          g_clear_error(&error);
          return NULL;
        }

      if (!filterx_object_is_type_or_ref(self, &FILTERX_TYPE_NAME(sequence)))
        {
          filterx_object_unref(self);
          return NULL;
        }
      return self;
    }

  filterx_eval_push_error_info_printf("Failed to create list", s,
                                      "Argument must be a list or a string, got: %s",
                                      filterx_object_get_type_name(arg));
  return NULL;
}

FILTERX_DEFINE_TYPE(list, FILTERX_TYPE_NAME(sequence),
                    .is_mutable = TRUE,
                    .truthy = _filterx_list_truthy,
                    .free_fn = _filterx_list_free,
                    .marshal = _filterx_list_marshal,
                    .repr = _filterx_list_repr,
                    .clone = _filterx_list_clone,
                    .clone_container = _filterx_list_clone_container,
                    .get_subscript = _filterx_list_get_subscript,
                    .set_subscript = _filterx_list_set_subscript,
                    .is_key_set = _filterx_list_is_key_set,
                    .unset_key = _filterx_list_unset_key,
                    .iter = _filterx_list_iter,
                    .len = _filterx_list_len,
                    .make_readonly = _filterx_list_make_readonly,
                    .freeze = _filterx_list_freeze,
                   );

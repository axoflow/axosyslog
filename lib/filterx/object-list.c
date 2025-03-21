/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/object-list.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict.h"
#include "filterx/object-dict-interface.h"
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
typedef struct _FilterXListObject
{
  FilterXList super;
  GPtrArray *array;
} FilterXListObject;

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
_filterx_list_get_subscript(FilterXList *s, guint64 index)
{
  FilterXListObject *self = (FilterXListObject *) s;

  if (index < self->array->len)
    {
      FilterXObject *value = g_ptr_array_index(self->array, index);
      return filterx_object_ref(value);
    }
  else
    return NULL;
}

static gboolean
_filterx_list_set_subscript(FilterXList *s, guint64 index, FilterXObject **new_value)
{
  FilterXListObject *self = (FilterXListObject *) s;

  if (index >= self->array->len)
    g_ptr_array_set_size(self->array, index + 1);

  FilterXObject *stored_object = filterx_ref_new(filterx_object_ref(*new_value));

  FilterXObject **slot = (FilterXObject **) &g_ptr_array_index(self->array, index);
  filterx_object_unref(*slot);
  *slot = filterx_object_ref(stored_object);
  return TRUE;
}

static gboolean
_filterx_list_append(FilterXList *s, FilterXObject **new_value)
{
  FilterXListObject *self = (FilterXListObject *) s;

  g_ptr_array_add(self->array, filterx_object_ref(*new_value));
  return TRUE;
}

static gboolean
_filterx_list_unset_index(FilterXList *s, guint64 index)
{
  FilterXListObject *self = (FilterXListObject *) s;
  g_assert(index <= self->array->len);
  g_ptr_array_remove_index(self->array, index);
  return TRUE;
}

static guint64
_filterx_list_len(FilterXList *s)
{
  FilterXListObject *self = (FilterXListObject *) s;

  return self->array->len;
}

static FilterXObject *
_filterx_list_factory(FilterXObject *self)
{
  return filterx_list_new();
}

static FilterXObject *
_filterx_dict_factory(FilterXObject *self)
{
  return filterx_dict_new();
}

FilterXObject *
filterx_list_new(void)
{
  FilterXListObject *self = g_new0(FilterXListObject, 1);
  filterx_list_init_instance(&self->super, &FILTERX_TYPE_NAME(list_object));

  self->super.get_subscript = _filterx_list_get_subscript;
  self->super.set_subscript = _filterx_list_set_subscript;
  self->super.append = _filterx_list_append;
  self->super.unset_index = _filterx_list_unset_index;
  self->super.len = _filterx_list_len;
  self->array = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_object_unref);
  return &self->super.super;
}

static FilterXObject *
_filterx_list_clone(FilterXObject *s)
{
  FilterXListObject *self = (FilterXListObject *) s;
  FilterXListObject *clone = (FilterXListObject *) filterx_list_new();

  for (gsize i = 0; i < self->array->len; i++)
    g_ptr_array_add(clone->array, filterx_object_clone(g_ptr_array_index(self->array, i)));
  return &clone->super.super;
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
      if (!filterx_list_set_subscript(list, i, &value))
        {
          filterx_object_unref(value);
          filterx_object_unref(list);
          list = NULL;
          break;
        }
      filterx_object_unref(value);
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
  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(list_object)))
    return filterx_object_ref(arg);

  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(list)))
    {
      FilterXObject *self = filterx_list_new();
      if (!filterx_list_merge(self, arg_unwrapped))
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
          filterx_eval_push_error_info("Argument must be a valid JSON string", s,
                                       g_strdup(error->message), TRUE);
          g_clear_error(&error);
          return NULL;
        }

      if (!filterx_object_is_type(self, &FILTERX_TYPE_NAME(list)))
        {
          filterx_object_unref(self);
          return NULL;
        }
      return self;
    }

  filterx_eval_push_error_info("Argument must be a list or a string", s,
                               g_strdup_printf("got \"%s\" instead", arg_unwrapped->type->name), TRUE);
  return NULL;
}

static gboolean
_freeze_list_item(gsize index, FilterXObject **value, gpointer user_data)
{
  filterx_object_freeze(value);
  return TRUE;
}

static void
_filterx_list_freeze(FilterXObject **s)
{
  FilterXListObject *self = (FilterXListObject *) *s;

  filterx_list_foreach(self, _freeze_list_item, NULL);
}

static gboolean
_unfreeze_list_item(gsize index, FilterXObject **value, gpointer user_data)
{
  filterx_object_unfreeze(*value);
  return TRUE;
}

static void
_filterx_list_unfreeze(FilterXObject *s)
{
  FilterXListObject *self = (FilterXListObject *) s;

  filterx_list_foreach(self, _unfreeze_list_item, NULL);
}


FILTERX_DEFINE_TYPE(list_object, FILTERX_TYPE_NAME(list),
                    .is_mutable = TRUE,
                    .truthy = _filterx_list_truthy,
                    .free_fn = _filterx_list_free,
                    .dict_factory = _filterx_dict_factory,
                    .list_factory = _filterx_list_factory,
                    .marshal = _filterx_list_marshal,
                    .repr = _filterx_list_repr,
                    .clone = _filterx_list_clone,
                    .freeze = _filterx_list_freeze,
                    .unfreeze = _filterx_list_unfreeze,
                   );

/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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
#include "object-metrics-labels.h"

static gboolean
_truthy(FilterXObject *s)
{
  // TODO: implement
  return FALSE;
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  // TODO: implement
  return FALSE;
}

static gboolean
_map_to_json(FilterXObject *s, struct json_object **object, FilterXObject **assoc_object)
{
  // TODO: implement
  return FALSE;
}

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  // TODO: implement
  return FALSE;
}

static FilterXObject *
_add(FilterXObject *s, FilterXObject *object)
{
  // TODO: implement
  return NULL;
}

static gboolean
_len(FilterXObject *s, guint64 *len)
{
  // TODO: implement
  return FALSE;
}

static FilterXObject *
_get_subscript(FilterXObject *s, FilterXObject *key)
{
  // TODO: implement
  return NULL;
}

static gboolean
_set_subscript(FilterXObject *s, FilterXObject *key, FilterXObject **new_value)
{
  // TODO: implement
  return FALSE;
}

static gboolean
_is_key_set(FilterXObject *s, FilterXObject *key)
{
  // TODO: implement
  return FALSE;
}

static gboolean
_unset_key(FilterXObject *s, FilterXObject *key)
{
  // TODO: implement
  return FALSE;
}

static FilterXObject *
_getattr(FilterXObject *s, FilterXObject *attr)
{
  // TODO: implement
  return NULL;
}

static gboolean
_setattr(FilterXObject *s, FilterXObject *attr, FilterXObject **new_value)
{
  // TODO: implement
  return FALSE;
}

static void
_free(FilterXObject *s)
{
  // TODO: implement
}

FilterXObject *
filterx_object_metrics_labels_new(guint reserved_size)
{
  // TODO: implement
  return NULL;
}

FilterXObject *
filterx_simple_function_metrics_labels(FilterXExpr *s, GPtrArray *args)
{
  // TODO: implement
  return NULL;
}

FILTERX_DEFINE_TYPE(metrics_labels, FILTERX_TYPE_NAME(object),
                    .is_mutable = TRUE,
                    .truthy = _truthy,
                    .map_to_json = _map_to_json,
                    .marshal = _marshal,
                    .repr = _repr,
                    .add = _add,
                    .len = _len,
                    .get_subscript = _get_subscript,
                    .set_subscript = _set_subscript,
                    .is_key_set = _is_key_set,
                    .unset_key = _unset_key,
                    .getattr = _getattr,
                    .setattr = _setattr,
                    .free_fn = _free,
                   );

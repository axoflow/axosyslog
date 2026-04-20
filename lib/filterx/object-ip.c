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
#include "filterx/object-ip.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-globals.h"
#include "filterx/object-extractor.h"
#include "gsocket.h"
#include <string.h>
#include <arpa/inet.h>

typedef struct _FilterXIP
{
  FilterXObject super;
  /* AF_INET or AF_INET6 */
  gint family;
  union
  {
    struct in_addr ipv4;
    struct in6_addr ipv6;
  };
} FilterXIP;

static gboolean
_ip_truthy(FilterXObject *s)
{
  return TRUE;
}

static void
_ip_to_string(FilterXIP *self, GString *target)
{
  gchar buf[INET6_ADDRSTRLEN];

  switch (self->family)
    {
    case AF_INET:
      g_inet_ntoa(buf, sizeof(buf), self->ipv4);
      break;
    case AF_INET6:
      inet_ntop(AF_INET6, &self->ipv6, buf, sizeof(buf));
      break;
    default:
      g_assert_not_reached();
    }
  g_string_append(target, buf);
}

static gboolean
_ip_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXIP *self = (FilterXIP *) s;

  *t = LM_VT_STRING;
  _ip_to_string(self, repr);
  return TRUE;
}

static gboolean
_ip_format_json(FilterXObject *s, GString *repr)
{
  FilterXIP *self = (FilterXIP *) s;

  g_string_append_c(repr, '"');
  _ip_to_string(self, repr);
  g_string_append_c(repr, '"');
  return TRUE;
}

static gboolean
_ip_repr(FilterXObject *s, GString *repr)
{
  FilterXIP *self = (FilterXIP *) s;

  g_string_append(repr, "ip('");
  _ip_to_string(self, repr);
  g_string_append(repr, "')");
  return TRUE;
}

static FilterXIP *
_ip_new(void)
{
  FilterXIP *self = filterx_new_object(FilterXIP);

  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(ip));
  return self;
}

static FilterXObject *
_ip_clone(FilterXObject *s)
{
  FilterXIP *self = (FilterXIP *) s;
  FilterXIP *clone = _ip_new();

  clone->family = self->family;
  switch (self->family)
    {
    case AF_INET:
      clone->ipv4 = self->ipv4;
      break;
    case AF_INET6:
      clone->ipv6 = self->ipv6;
      break;
    default:
      g_assert_not_reached();
    }
  return &clone->super;
}

const struct in_addr *
filterx_ip_get_v4(FilterXObject *obj)
{
  if (!filterx_object_is_type(obj, &FILTERX_TYPE_NAME(ip)))
    return NULL;
  FilterXIP *self = (FilterXIP *) obj;
  if (self->family != AF_INET)
    return NULL;
  return &self->ipv4;
}

const struct in6_addr *
filterx_ip_get_v6(FilterXObject *obj)
{
  if (!filterx_object_is_type(obj, &FILTERX_TYPE_NAME(ip)))
    return NULL;
  FilterXIP *self = (FilterXIP *) obj;
  if (self->family != AF_INET6)
    return NULL;
  return &self->ipv6;
}

/* NOTE: returns NULL for invalid IP addresses */
FilterXObject *
filterx_ip_new_from_string(const gchar *ip_str)
{
  FilterXIP *self = _ip_new();

  if (inet_aton(ip_str, &self->ipv4) == 1)
    {
      self->family = AF_INET;
      return &self->super;
    }

  if (inet_pton(AF_INET6, ip_str, &self->ipv6) == 1)
    {
      self->family = AF_INET6;
      return &self->super;
    }

  filterx_object_unref(&self->super);
  return NULL;
}

FILTERX_DEFINE_TYPE(ip, FILTERX_TYPE_NAME(object),
                    .clone = _ip_clone,
                    .truthy = _ip_truthy,
                    .format_json = _ip_format_json,
                    .marshal = _ip_marshal,
                    .repr = _ip_repr,
                   );

FilterXObject *
filterx_typecast_ip(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(ip)))
    {
      filterx_object_ref(object);
      return object;
    }

  const gchar *str;
  if (!filterx_object_extract_string_as_cstr(object, &str))
    {
      filterx_eval_push_error_static_info("Failed to cast to ip()", s, "Argument is not a string");
      return NULL;
    }

  object = filterx_ip_new_from_string(str);
  if (!object)
    {
      filterx_eval_push_error_static_info("Failed to cast to ip()", s, "Argument is not a valid IP address");
      return NULL;
    }
  return object;
}

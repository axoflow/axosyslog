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
#include "filterx/object-subnet.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-globals.h"
#include "filterx/object-extractor.h"
#include "gsocket.h"
#include "str-format.h"

typedef struct _FilterXSubnet
{
  FilterXObject super;
  /* AF_INET or AF_INET6 */
  gint family;
  union
  {
    struct
    {
      struct in_addr address;
      struct in_addr netmask;
    } ipv4;
    struct
    {
      struct in6_addr address;
      struct in6_addr netmask;
    } ipv6;
  };
} FilterXSubnet;

static gboolean
_subnet_truthy(FilterXObject *s)
{
  FilterXSubnet *self = (FilterXSubnet *) s;
  switch (self->family)
    {
    case AF_INET:
      return self->ipv4.address.s_addr != INADDR_ANY;
    case AF_INET6:
      return memcmp(self->ipv6.address.s6_addr, in6addr_any.s6_addr, sizeof(in6addr_any.s6_addr)) == 0;
    default:
      g_assert_not_reached();
    }
}

static void
_subnet_ipv4_to_string(FilterXSubnet *self, GString *target)
{
  gchar buf[32];

  g_inet_ntoa(buf, sizeof(buf), self->ipv4.address);
  g_string_append(target, buf);
  g_string_append_c(target, '/');
  g_inet_ntoa(buf, sizeof(buf), self->ipv4.netmask);
  g_string_append(target, buf);
}

static void
_subnet_ipv6_to_string(FilterXSubnet *self, GString *target)
{
  gchar buf[32];

  inet_ntop(AF_INET6, &self->ipv6.address, buf, sizeof(buf));
  g_string_append(target, buf);
  g_string_append_c(target, '/');
  inet_ntop(AF_INET6, &self->ipv6.netmask, buf, sizeof(buf));
  g_string_append(target, buf);
}


static void
_subnet_to_string(FilterXSubnet *self, GString *target)
{
  switch (self->family)
    {
    case AF_INET:
      _subnet_ipv4_to_string(self, target);
      break;
    case AF_INET6:
      _subnet_ipv6_to_string(self, target);
      break;
    default:
      g_assert_not_reached();
    }
}

static gboolean
_subnet_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXSubnet *self = (FilterXSubnet *) s;

  *t = LM_VT_STRING;
  _subnet_to_string(self, repr);
  return TRUE;
}

static gboolean
_subnet_format_json(FilterXObject *s, GString *repr)
{
  FilterXSubnet *self = (FilterXSubnet *) s;

  g_string_append_c(repr, '"');
  _subnet_to_string(self, repr);
  g_string_append_c(repr, '"');
  return TRUE;
}

static gboolean
_subnet_repr(FilterXObject *s, GString *repr)
{
  FilterXSubnet *self = (FilterXSubnet *) s;

  g_string_append(repr, "subnet('");
  _subnet_to_string(self, repr);
  g_string_append(repr, "')");
  return TRUE;
}

static FilterXSubnet *
_subnet_new(void)
{
  FilterXSubnet *self = filterx_new_object(FilterXSubnet);

  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(subnet));
  return self;
}

static FilterXObject *
_subnet_clone(FilterXObject *s)
{
  FilterXSubnet *self = (FilterXSubnet *) s;
  FilterXSubnet *clone = _subnet_new();

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

static gboolean
_parse_ipv4_cidr(FilterXSubnet *self, const gchar *cidr)
{
  gchar buf[64];

  const gchar *slash = strchr(cidr, '/');
  if (strlen(cidr) >= sizeof(buf) || !slash)
    {
      if (inet_aton(cidr, &self->ipv4.address) != 1)
        return FALSE;
      self->ipv4.netmask.s_addr = htonl(0xFFFFFFFF);
    }
  else
    {
      strncpy(buf, cidr, slash - cidr);
      buf[slash - cidr] = 0;
      if (inet_aton(buf, &self->ipv4.address) != 1)
        return FALSE;

      if (strchr(slash + 1, '.'))
        {
          if (inet_aton(slash + 1, &self->ipv4.netmask) != 1)
            return FALSE;
        }
      else
        {
          gchar *endptr = NULL;
          gint prefix = strtol(slash + 1, &endptr, 10);
          if (*endptr != 0)
            return FALSE;
          if (prefix > 32)
            return FALSE;
          if (prefix == 32)
            self->ipv4.netmask.s_addr = htonl(0xFFFFFFFF);
          else
            self->ipv4.netmask.s_addr = htonl(((1 << prefix) - 1) << (32 - prefix));
        }
    }
  self->family = AF_INET;
  self->ipv4.address.s_addr &= self->ipv4.netmask.s_addr;
  return TRUE;
}

static void
_derive_ipv6_netmask(FilterXSubnet *self, gint prefix)
{
  gint pos = 0;
  memset(self->ipv6.netmask.s6_addr, 0xFF, prefix / 8);
  pos += prefix / 8;
  if (pos < sizeof(self->ipv6.netmask.s6_addr))
    {
      gint bit_count = prefix % 8;
      self->ipv6.netmask.s6_addr[pos] = ((1 << bit_count) - 1) << (8 - bit_count);
      pos++;
      memset(&self->ipv6.netmask.s6_addr[pos], 0x00, sizeof(self->ipv6.netmask.s6_addr) - pos);
    }
}

static void
_apply_ipv6_mask(FilterXSubnet *self, struct in6_addr *addr)
{
  for (gint i = 0; i < sizeof(addr->s6_addr); i++)
    {
      addr->s6_addr[i] &= self->ipv6.netmask.s6_addr[i];
    }
}

static gboolean
_parse_ipv6_cidr(FilterXSubnet *self, const gchar *cidr)
{
  gchar buf[INET6_ADDRSTRLEN];
  const gchar *slash = strchr(cidr, '/');
  gint prefix = 0;

  if (strlen(cidr) >= sizeof(buf) || !slash)
    {
      if (inet_pton(AF_INET6, cidr, &self->ipv6.address) != 1)
        return FALSE;
      prefix = 128;
    }
  else
    {
      gchar *endptr = NULL;
      prefix = strtol(slash + 1, &endptr, 10);
      if (*endptr != 0)
        return FALSE;
      if (prefix > 128)
        return FALSE;

      strncpy(buf, cidr, slash - cidr);
      buf[slash - cidr] = 0;
      if (inet_pton(AF_INET6, buf, &self->ipv6.address) != 1)
        return FALSE;
    }

  _derive_ipv6_netmask(self, prefix);
  _apply_ipv6_mask(self, &self->ipv6.address);

  self->family = AF_INET6;
  return TRUE;
}

static FilterXObject *
_subnet_is_string_member_of(FilterXSubnet *self, FilterXObject *member)
{
  const gchar *str;
  if (!filterx_object_extract_string_as_cstr(member, &str))
    {
      filterx_eval_push_error("Failed to check subnet() membership, argument is not a string or ip()", NULL, member);
      return NULL;
    }
  switch (self->family)
    {
    case AF_INET:
      {
        struct in_addr addr;
        if (g_inet_aton(str, &addr) == 1)
          return filterx_boolean_new((addr.s_addr & self->ipv4.netmask.s_addr) == (self->ipv4.address.s_addr));
        break;
      }
    case AF_INET6:
      {
        struct in6_addr addr;

        if (inet_pton(AF_INET6, str, &addr) == 1)
          {
            _apply_ipv6_mask(self, &addr);
            gboolean address_in_range = memcmp(addr.s6_addr, self->ipv6.address.s6_addr, sizeof(addr.s6_addr)) == 0;

            return filterx_boolean_new(address_in_range);
          }
        break;
      }
    default:
      g_assert_not_reached();
    }
  filterx_eval_push_error("Failed to check subnet() membership, error parsing IP address", NULL, member);
  return NULL;
}

static FilterXObject *
_subnet_is_member_of(FilterXObject *s, FilterXObject *member)
{
  FilterXSubnet *self = (FilterXSubnet *) s;
  FilterXObject *result;

  const gchar *str;
  if (!filterx_object_extract_string_as_cstr(member, &str))
    {
      filterx_eval_push_error("Failed to check subnet() membership, argument is not a string", NULL, member);
      return NULL;
    }

  return _subnet_is_string_member_of(self, member);
}

/* NOTE: returns NULL for invalid CIDR values */
FilterXObject *
filterx_subnet_new_from_cidr(const gchar *cidr)
{
  FilterXSubnet *self = _subnet_new();

  if (_parse_ipv4_cidr(self, cidr))
    return &self->super;

  if (_parse_ipv6_cidr(self, cidr))
    return &self->super;

  filterx_object_unref(&self->super);
  return NULL;
}

FILTERX_DEFINE_TYPE(subnet, FILTERX_TYPE_NAME(object),
                    .clone = _subnet_clone,
                    .truthy = _subnet_truthy,
                    .format_json = _subnet_format_json,
                    .marshal = _subnet_marshal,
                    .repr = _subnet_repr,
                    .is_member_of = _subnet_is_member_of,
                   );

FilterXObject *
filterx_typecast_subnet(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(subnet)))
    {
      filterx_object_ref(object);
      return object;
    }

  const gchar *str;
  if (!filterx_object_extract_string_as_cstr(object, &str))
    {
      filterx_eval_push_error_static_info("Failed to cast string to subnet()", s, "Argument is not a string");
      return NULL;
    }

  object = filterx_subnet_new_from_cidr(str);
  if (!object)
    {
      filterx_eval_push_error_static_info("Failed to cast string to subnet()", s, "Argument is not well formed CIDR");
      return NULL;
    }
  return object;
}

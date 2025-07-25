/*
 * Copyright (c) 2002-2013 Balabit
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
#include "cache.h"

struct _Cache
{
  GHashTable *hash_table;
  CacheResolver *resolver;
};

void *
cache_resolve(Cache *self, const gchar *key)
{
  return cache_resolver_resolve_elem(self->resolver, key);
}

void *
cache_lookup(Cache *self, const gchar *key)
{
  gpointer result = g_hash_table_lookup(self->hash_table, key);

  if (!result)
    {
      result = cache_resolve(self, key);
      if (result)
        {
          g_hash_table_insert(self->hash_table, g_strdup(key), result);
        }
    }
  return result;
}

void
cache_populate(Cache *self, const gchar *key, const gchar *value)
{
  gpointer result = g_hash_table_lookup(self->hash_table, key);

  g_assert(result == NULL);
  g_hash_table_insert(self->hash_table, g_strdup(key), g_strdup(value));
}

void
cache_clear(Cache *self)
{
  g_hash_table_unref(self->hash_table);
  self->hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, self->resolver->free_elem);
}

Cache *
cache_new(CacheResolver *resolver)
{
  Cache *self = g_new0(Cache, 1);

  self->resolver = resolver;
  self->hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, resolver->free_elem);
  return self;
}

void
cache_free(Cache *self)
{
  cache_resolver_free(self->resolver);
  g_hash_table_unref(self->hash_table);
  g_free(self);
}

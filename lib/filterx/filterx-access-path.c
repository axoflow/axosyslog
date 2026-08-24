/*
 * Copyright (c) 2026 Axoflow
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

#include "filterx/filterx-access-path.h"

#include <string.h>

static GHashTable *key_pool;

const gchar *
filterx_access_path_intern_key(const gchar *key)
{
  if (!key)
    return NULL;

  if (!key_pool)
    key_pool = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  const gchar *interned = g_hash_table_lookup(key_pool, key);
  if (!interned)
    {
      interned = g_strdup(key);
      g_hash_table_insert(key_pool, (gpointer) interned, (gpointer) interned);
    }

  return interned;
}

/* Steps are interned, so pointer equality settles it; the strcmp() is what gives the GTree a
 * deterministic order across runs. */
static inline gint
_step_compare(const gchar *a, const gchar *b)
{
  if (a == b)
    return 0;
  return strcmp(a, b);
}

gint
filterx_access_path_compare(gconstpointer a, gconstpointer b, gpointer user_data)
{
  const FilterXAccessPath *pa = (const FilterXAccessPath *) a;
  const FilterXAccessPath *pb = (const FilterXAccessPath *) b;

  if (pa->root != pb->root)
    return pa->root < pb->root ? -1 : 1;

  guint common = MIN(pa->n_steps, pb->n_steps);
  for (guint i = 0; i < common; i++)
    {
      gint c = _step_compare(pa->steps[i], pb->steps[i]);
      if (c != 0)
        return c;
    }

  if (pa->n_steps != pb->n_steps)
    return pa->n_steps < pb->n_steps ? -1 : 1;
  return 0;
}

void
filterx_access_path_init(FilterXAccessPath *self)
{
  memset(self, 0, sizeof(*self));
}

FilterXAccessPath *
filterx_access_path_dup(const FilterXAccessPath *self)
{
  FilterXAccessPath *copy = g_new(FilterXAccessPath, 1);
  *copy = *self;
  return copy;
}

void
filterx_access_path_release_keys(void)
{
  g_clear_pointer(&key_pool, g_hash_table_destroy);
}

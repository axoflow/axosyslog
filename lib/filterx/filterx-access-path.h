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
#ifndef FILTERX_ACCESS_PATH_H_INCLUDED
#define FILTERX_ACCESS_PATH_H_INCLUDED

#include "syslog-ng.h"
#include "filterx/filterx-variable.h"

#define FILTERX_ACCESS_PATH_MAX_DEPTH 8

/* Addresses a location inside a variable: the root variable's handle plus the key steps leading to
 * it.  The zero-step path addresses the variable itself.
 *
 * Steps are interned because FilterXExpr::name borrows its characters from an object the
 * expression frees, and interning is also what makes two steps compare by pointer. */
typedef struct _FilterXAccessPath
{
  FilterXVariableHandle root;
  guint n_steps;
  /* steps[] is only a prefix of the location: it nests deeper than FILTERX_ACCESS_PATH_MAX_DEPTH, or
   * a step is a key no literal names (a computed subscript, a list index, the `expr[] = v`
   * append). */
  gboolean truncated;
  const gchar *steps[FILTERX_ACCESS_PATH_MAX_DEPTH];
} FilterXAccessPath;

void filterx_access_path_init(FilterXAccessPath *self);
FilterXAccessPath *filterx_access_path_dup(const FilterXAccessPath *self);

const gchar *filterx_access_path_intern_key(const gchar *key);
void filterx_access_path_release_keys(void);

/* Orders a path immediately before its own descendants, so every subtree is a contiguous range. */
gint filterx_access_path_compare(gconstpointer a, gconstpointer b, gpointer user_data);

static inline gboolean
filterx_access_path_is_prefix_of(const FilterXAccessPath *prefix, const FilterXAccessPath *path)
{
  if (prefix->root != path->root || prefix->n_steps > path->n_steps)
    return FALSE;
  for (guint i = 0; i < prefix->n_steps; i++)
    {
      if (prefix->steps[i] != path->steps[i])
        return FALSE;
    }
  return TRUE;
}

static inline void
filterx_access_path_parent(const FilterXAccessPath *self, FilterXAccessPath *parent_out)
{
  *parent_out = *self;
  if (parent_out->n_steps > 0)
    parent_out->n_steps--;
}

/* Appending to a truncated path keeps it truncated: `d[$k].a` must not read as `d.a`, the write
 * having gone under some key of d rather than under that one. */
static inline gboolean
filterx_access_path_append_step(FilterXAccessPath *self, const gchar *step)
{
  if (self->truncated)
    return FALSE;

  if (!step || self->n_steps >= FILTERX_ACCESS_PATH_MAX_DEPTH)
    {
      self->truncated = TRUE;
      return FALSE;
    }
  self->steps[self->n_steps++] = step;
  return TRUE;
}

#endif

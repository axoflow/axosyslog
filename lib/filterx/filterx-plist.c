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
#include "filterx-plist.h"

void
filterx_pointer_list_add(FilterXPointerList *self, gpointer value)
{
  g_assert(self->mode == FPL_MUTABLE);
  g_ptr_array_add(self->mut.pointers, value);
}

void
filterx_pointer_list_add_list(FilterXPointerList *self, GList *elements)
{
  g_assert(self->mode == FPL_MUTABLE);

  for (GList *link = elements; link; link = link->next)
    {
      filterx_pointer_list_add(self, link->data);
    }
  g_list_free(elements);
}

gboolean
filterx_pointer_list_foreach_ref(FilterXPointerList *self, FilterXPointerListForeachRefFunc func, gpointer user_data)
{
  gpointer *pointers;
  gsize pointers_len;

  if (self->mode == FPL_MUTABLE)
    {
      pointers = self->mut.pointers->pdata;
      pointers_len = self->mut.pointers->len;
    }
  else
    {
      pointers = self->sealed.pointers;
      pointers_len = self->sealed.pointers_len;
    }
  for (gsize i = 0; i < pointers_len; i++)
    {
      if (!func(&pointers[i], user_data))
        return FALSE;
    }
  return TRUE;
}

static gboolean
_invoke_non_ref(gpointer *pvalue, gpointer user_data)
{
  FilterXPointerListForeachFunc func = ((gpointer *) user_data)[0];
  gpointer user_data2 = ((gpointer *) user_data)[1];

  return func(*pvalue, user_data2);
}

static gboolean
_invoke_clear(gpointer *pvalue, gpointer d)
{
  GDestroyNotify func = (GDestroyNotify) d;

  func(*pvalue);

  return TRUE;
}

gboolean
filterx_pointer_list_foreach(FilterXPointerList *self, FilterXPointerListForeachFunc func, gpointer user_data)
{
  return filterx_pointer_list_foreach_ref(self, _invoke_non_ref, (gpointer [])
  {
    func, user_data
  });
}

void
filterx_pointer_list_seal(FilterXPointerList *self)
{
  if (self->mode == FPL_MUTABLE)
    {
      self->mode = FPL_SEALED;
      self->sealed.pointers_len = self->mut.pointers->len;
      self->sealed.pointers = g_ptr_array_free(self->mut.pointers, FALSE);
    }
}

void
filterx_pointer_list_init(FilterXPointerList *self)
{
  memset(self, 0, sizeof(*self));
  self->mut.pointers = g_ptr_array_new();
  self->mode = FPL_MUTABLE;
}

void
filterx_pointer_list_clear(FilterXPointerList *self, GDestroyNotify destroy)
{
  if (destroy)
    filterx_pointer_list_foreach_ref(self, _invoke_clear, destroy);

  switch (self->mode)
    {
    case FPL_MUTABLE:
      g_ptr_array_free(self->mut.pointers, TRUE);
      break;
    case FPL_SEALED:
      g_free(self->sealed.pointers);
      break;
    default:
      g_assert_not_reached();
      break;
    }
}

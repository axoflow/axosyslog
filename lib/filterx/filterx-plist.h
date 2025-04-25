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

#ifndef FILTERX_PLIST_H_INCLUDED
#define FILTERX_PLIST_H_INCLUDED 1

#include "filterx-expr.h"

/* encapsulates a mutable list which is then "sealed" to make it unmutable
 * and avoid the extra pointer dereference (e.g. GPtrArray->pdata[0]) */
typedef gboolean (*FilterXPointerListForeachRefFunc)(gpointer *value, gpointer user_data);
typedef gboolean (*FilterXPointerListForeachFunc)(gpointer value, gpointer user_data);
typedef struct _FilterXPointerList
{
  enum
  {
    /* the pointer list is still mutable */
    FPL_MUTABLE,

    /* sealed mode unwraps the elements from the GPtrArray for faster,
     * read-only access */
    FPL_SEALED,
  } mode;
  union
  {
    struct
    {
      GPtrArray *pointers;
    } mut;
    struct
    {
      gpointer *pointers;
      guint32 pointers_len;
    } sealed;
  };
} FilterXPointerList;

gboolean filterx_pointer_list_foreach_ref(FilterXPointerList *self,
                                          FilterXPointerListForeachRefFunc func, gpointer user_data);
gboolean filterx_pointer_list_foreach(FilterXPointerList *self,
                                      FilterXPointerListForeachFunc func, gpointer user_data);
void filterx_pointer_list_add(FilterXPointerList *self, gpointer value);
void filterx_pointer_list_add_list(FilterXPointerList *self, GList *elements);

void filterx_pointer_list_seal(FilterXPointerList *self);
void filterx_pointer_list_init(FilterXPointerList *self);
void filterx_pointer_list_clear(FilterXPointerList *self, GDestroyNotify destroy);

static inline gpointer
filterx_pointer_list_index(FilterXPointerList *self, gsize index)
{
  switch (self->mode)
    {
    case FPL_SEALED:
      g_assert(index < self->sealed.pointers_len);
      return self->sealed.pointers[index];
    case FPL_MUTABLE:
      return g_ptr_array_index(self->mut.pointers, index);
    default:
      g_assert_not_reached();
      break;
    }
}

static inline gpointer
filterx_pointer_list_index_fast(FilterXPointerList *self, gsize index)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(self->mode != FPL_MUTABLE);
#endif
  return self->sealed.pointers[index];
}

static inline gsize
filterx_pointer_list_get_length(FilterXPointerList *self)
{
  if (self->mode == FPL_MUTABLE)
    return self->mut.pointers->len;
  else
    return self->sealed.pointers_len;
}

typedef gboolean (*FilterXExprListForeachRefFunc)(FilterXExpr **pvalue, gpointer user_data);
typedef gboolean (*FilterXExprListForeachFunc)(FilterXExpr *value, gpointer user_data);
typedef FilterXPointerList FilterXExprList;

static inline gboolean
filterx_expr_list_foreach_ref(FilterXExprList *self, FilterXExprListForeachRefFunc func, gpointer user_data)
{
  return filterx_pointer_list_foreach_ref(self, (FilterXPointerListForeachRefFunc) func, user_data);
}

static inline gboolean
filterx_expr_list_foreach(FilterXExprList *self, FilterXExprListForeachFunc func, gpointer user_data)
{
  return filterx_pointer_list_foreach(self, (FilterXPointerListForeachFunc) func, user_data);
}

static inline void
filterx_expr_list_seal(FilterXExprList *self)
{
  filterx_pointer_list_seal(self);
}

static inline void
filterx_expr_list_add(FilterXExprList *self, FilterXExpr *expr)
{
  filterx_pointer_list_add(self, filterx_expr_ref(expr));
}

static inline FilterXExpr *
filterx_expr_list_index(FilterXExprList *self, gsize index)
{
  return (FilterXExpr *) filterx_pointer_list_index(self, index);
}

static inline FilterXExpr *
filterx_expr_list_index_fast(FilterXExprList *self, gsize index)
{
  return (FilterXExpr *) filterx_pointer_list_index_fast(self, index);
}

static inline gsize
filterx_expr_list_get_length(FilterXExprList *self)
{
  return filterx_pointer_list_get_length(self);
}


static inline void
filterx_expr_list_init(FilterXExprList *self)
{
  filterx_pointer_list_init(self);
}

static inline void
filterx_expr_list_clear(FilterXExprList *self)
{
  filterx_pointer_list_clear(self, (GDestroyNotify) filterx_expr_unref);
}

#endif

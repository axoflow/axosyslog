

#ifndef FILTERX_EXPR_LIST_H_INCLUDED
#define FILTERX_EXPR_LIST_H_INCLUDED 1

#include "filterx-expr.h"

/* encapsulates a mutable list which is then "sealed" to make it unmutable
 * and avoid the extra pointer dereference (e.g. GPtrArray->pdata[0]) */
typedef gboolean (*FilterXPointerListForeachRefFunc)(gpointer *value, gpointer user_data);
typedef gboolean (*FilterXPointerListForeachFunc)(gpointer value, gpointer user_data);
typedef struct _FilterXPointerList
{
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
  /* could be using bits here if we need more than this */
  gboolean is_sealed;
} FilterXPointerList;

gboolean filterx_pointer_list_foreach_ref(FilterXPointerList *self, FilterXPointerListForeachRefFunc func, gpointer user_data);
gboolean filterx_pointer_list_foreach(FilterXPointerList *self, FilterXPointerListForeachFunc func, gpointer user_data);
void filterx_pointer_list_add_list(FilterXPointerList *self, GList *elements);

void filterx_pointer_list_init(FilterXPointerList *self);
void filterx_pointer_list_clear(FilterXPointerList *self, GDestroyNotify destroy);

static inline gboolean
filterx_pointer_list_is_sealed(FilterXPointerList *self)
{
  return self->is_sealed;
}

static inline void
filterx_pointer_list_seal(FilterXPointerList *self)
{
  if (!self->is_sealed)
    {
      self->is_sealed = TRUE;
      self->sealed.pointers_len = self->mut.pointers->len;
      self->sealed.pointers = g_ptr_array_free(self->mut.pointers, FALSE);
    }
}

static inline void
filterx_pointer_list_add(FilterXPointerList *self, gpointer value)
{
  g_assert(self->is_sealed == FALSE);
  g_ptr_array_add(self->mut.pointers, value);
}

static inline gpointer
filterx_pointer_list_index(FilterXPointerList *self, gsize index)
{
  if (self->is_sealed)
    {
      g_assert(index < self->sealed.pointers_len);
      return self->sealed.pointers[index];
    }
  else
    return g_ptr_array_index(self->mut.pointers, index);
}

static inline gpointer
filterx_pointer_list_index_fast(FilterXPointerList *self, gsize index)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(self->is_sealed == TRUE);
#endif
  return self->sealed.pointers[index];
}

static inline gsize
filterx_pointer_list_get_length(FilterXPointerList *self)
{
  if (self->is_sealed)
    return self->sealed.pointers_len;
  else
    return self->mut.pointers->len;
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
  filterx_pointer_list_add(self, expr);
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



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

gboolean filterx_pointer_list_foreach(FilterXPointerList *self, FilterXPointerListForeachFunc func, gpointer user_data);

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
  self->is_sealed = TRUE;
  self->sealed.pointers_len = self->mut.pointers->len;
  self->sealed.pointers = g_ptr_array_free(self->mut.pointers, FALSE);
}

static inline void
filterx_pointer_list_add(FilterXPointerList *self, gpointer value)
{
  g_assert(self->is_sealed == FALSE);
  g_ptr_array_add(self->mut.pointers, value);
}

static inline gsize
filterx_pointer_list_get_length(FilterXPointerList *self)
{
  if (self->is_sealed)
    return self->sealed.pointers_len;
  else
    return self->mut.pointers->len;
}

#endif

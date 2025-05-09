#include "filterx-expr-list.h"

void
filterx_pointer_list_add_list(FilterXPointerList *self, GList *elements)
{
  g_assert(self->is_sealed == FALSE);

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

  if (self->is_sealed)
    {
      pointers = self->sealed.pointers;
      pointers_len = self->sealed.pointers_len;
    }
  else
    {
      pointers = self->mut.pointers->pdata;
      pointers_len = self->mut.pointers->len;
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

gboolean
filterx_pointer_list_foreach(FilterXPointerList *self, FilterXPointerListForeachFunc func, gpointer user_data)
{
  return filterx_pointer_list_foreach_ref(self, _invoke_non_ref, (gpointer []) {func, user_data});
}

void
filterx_pointer_list_init(FilterXPointerList *self)
{
  memset(self, 0, sizeof(*self));
  self->mut.pointers = g_ptr_array_new();
}

void
filterx_pointer_list_clear(FilterXPointerList *self, GDestroyNotify destroy)
{
  filterx_pointer_list_foreach(self, (FilterXPointerListForeachFunc) destroy, NULL);
  if (!filterx_pointer_list_is_sealed(self))
    g_ptr_array_free(self->mut.pointers, TRUE);
}

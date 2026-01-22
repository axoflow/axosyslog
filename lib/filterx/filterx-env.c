#include "filterx/filterx-env.h"

static FilterXObject *
_get_frozen_object(FilterXObjectFreezer *self, const gchar *key)
{
  FilterXEnvironment *env = (FilterXEnvironment *) self->user_data;

  return g_hash_table_lookup(env->deduplicated_objects, key);
}

static void
_add_frozen_object(FilterXObjectFreezer *self, gchar *key, FilterXObject *object)
{
  FilterXEnvironment *env = (FilterXEnvironment *) self->user_data;

  g_hash_table_insert(env->deduplicated_objects, key, object);
}

static void
_keep_frozen_object(FilterXObjectFreezer *self, FilterXObject *object)
{
  FilterXEnvironment *env = (FilterXEnvironment *) self->user_data;

  g_ptr_array_add(env->frozen_objects, object);
}

void
filterx_env_freezer_init(FilterXObjectFreezer *self, FilterXEnvironment *env)
{
  self->get = _get_frozen_object;
  self->add = _add_frozen_object;
  self->keep = _keep_frozen_object;
  self->user_data = env;
}

static void
_prepare_for_object_freeze(FilterXEnvironment *self)
{
  if (!self->frozen_objects)
    self->frozen_objects = g_ptr_array_new();
  if (!self->deduplicated_objects)
    self->deduplicated_objects = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

static void
_destroy_frozen_objects(GPtrArray *frozen_objects)
{
  if (!frozen_objects)
    return;
  /* keep all objects "FROZEN" and force call their destructor */
  for (gsize i = 0; i < frozen_objects->len; i++)
    {
      FilterXObject *o = (FilterXObject *) g_ptr_array_index(frozen_objects, i);
      o->type->free_fn(o);
    }
  /* we now have the empty shells, let's free them */
  for (gsize i = 0; i < frozen_objects->len; i++)
    {
      FilterXObject *o = (FilterXObject *) g_ptr_array_index(frozen_objects, i);
      filterx_free_object(o);
    }
  g_ptr_array_unref(frozen_objects);
}

void
filterx_env_freeze_object(FilterXEnvironment *self, FilterXObject **object)
{
  _prepare_for_object_freeze(self);

  FilterXObjectFreezer freezer;
  filterx_env_freezer_init(&freezer, self);
  filterx_object_freeze(object, &freezer);
}

void
filterx_env_move(FilterXEnvironment *target, FilterXEnvironment *source)
{
  target->frozen_objects = source->frozen_objects;
  target->deduplicated_objects = source->deduplicated_objects;
  target->weak_refs = source->weak_refs;
  source->frozen_objects = NULL;
  source->deduplicated_objects = NULL;
  source->weak_refs = NULL;
}

void
filterx_env_init(FilterXEnvironment *self)
{
  memset(self, 0, sizeof(*self));
  self->weak_refs = g_ptr_array_new_full(32, (GDestroyNotify) filterx_object_unref);
}

void
filterx_env_clear(FilterXEnvironment *self)
{
  g_ptr_array_unref(self->weak_refs);
  g_hash_table_unref(self->deduplicated_objects);
  _destroy_frozen_objects(self->frozen_objects);
}

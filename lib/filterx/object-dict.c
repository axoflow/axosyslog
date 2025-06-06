/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 László Várady
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-message-value.h"
#include "filterx/json-repr.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-eval.h"
#include "utf8utils.h"
#include "str-format.h"
#include "str-utils.h"
#include "scratch-buffers.h"

/* hash table implementation inspired by Python's own dict implementation,
 * as described in
 *
 * https://github.com/python/cpython/blob/main/Objects/dictobject.c
 */

typedef struct _FilterXDictEntry
{
  FilterXObject *key;
  FilterXObject *value;
} FilterXDictEntry;

static inline void
filterx_dict_entry_clear(FilterXDictEntry *entry)
{
  filterx_object_unref(entry->key);
  filterx_ref_unset_parent_container(entry->value);
  filterx_object_unref(entry->value);
  entry->key = entry->value = NULL;
}

/* index into the indices array, always positive */
typedef guint32 FilterXDictIndexSlot;
/* index into the entries array, can be negative because of the special values */
typedef gint32 FilterXDictEntrySlot;
enum
{
  FXD_IX_EMPTY = -1,
  FXD_IX_DUMMY = -2,
  /* any value >= is an actual index into the entries table */
};

#define FILTERX_DICT_MAX_SIZE G_MAXINT32
#define FILTERX_DICT_INDEX_SLOT_NONE G_MAXUINT32

typedef gboolean (*FilterXDictForeachFunc)(FilterXObject **, FilterXObject **, gpointer);
typedef struct _FilterXDictTable
{
  gint size_log2;
  guint32 size;
  guint32 mask;
  guint32 entries_size;
  guint32 entries_num;
  guint32 empty_num;

  /* indices can be an array of gint8, gint16, gint32 depending on the hash
   * table size, number of elements matches "size" */

  gchar indices[0];

  /* NOTE: after indices, we have the actual entries, e.g.  as if we had the
   * following declaration at the end of this struct.  The number of
   * elements is up to entries_size, normally the 2/3 of size
   *
   *      FilterXDictEntry entries[entries_size];
   */
} FilterXDictTable;


#if (defined(__clang__) || defined(__GNUC__))
static inline gint
_round_to_log2(gsize x)
{
  if (x == 0)
    return 0;

  /* __builtin_clzll(): Returns the number of leading 0-bits in X, starting
   * at the most significant bit position.  If X is 0, the result is
   * undefined.
   */
  return (gint) sizeof(x) * 8 - __builtin_clzll(x-1);
}
#else
#error "Only gcc/clang is supported to compile this code"
#endif

static inline gsize
_pow2(gint bits)
{
  return 1 << bits;
}

static inline gint
_table_index_element_size(gsize table_size)
{
  if (table_size < 256)
    return 1;
  if (table_size < 65536)
    return 2;
  if (table_size < G_MAXUINT32)
    return 4;
  return 8;
}

static inline gsize
_table_usable_entries(gsize table_size)
{
  return (table_size << 1) / 3;
}

static inline gsize
_table_index_size(FilterXDictTable *table)
{
  return table->size * _table_index_element_size(table->size);
}

/* get the value of an index entry, -> entry_slot */
static inline FilterXDictEntrySlot
_table_get_index_entry(FilterXDictTable *table, FilterXDictIndexSlot slot)
{
  gint element_size = _table_index_element_size(table->size);
  if (element_size == 1)
    {
      gint8 *indices = (gint8 *) table->indices;
      return indices[slot];
    }
  else if (element_size == 2)
    {
      gint16 *indices = (gint16 *) table->indices;
      return indices[slot];
    }
  else if (element_size == 4)
    {
      gint32 *indices = (gint32 *) table->indices;
      return indices[slot];
    }
  else if (element_size == 8)
    {
      gint64 *indices = (gint64 *) table->indices;
      return indices[slot];
    }
  g_assert_not_reached();
}

/* set the value of an index entry, := entry_slot */
static inline void
_table_set_index_entry(FilterXDictTable *table, gsize index, FilterXDictEntrySlot value)
{
  gint element_size = _table_index_element_size(table->size);
  if (element_size == 1)
    {
      gint8 *indices = (gint8 *) table->indices;
      indices[index] = value;
    }
  else if (element_size == 2)
    {
      gint16 *indices = (gint16 *) table->indices;
      indices[index] = value;
    }
  else if (element_size == 4)
    {
      gint32 *indices = (gint32 *) table->indices;
      indices[index] = value;
    }
  else if (element_size == 8)
    {
      gint64 *indices = (gint64 *) table->indices;
      indices[index] = value;
    }
  else
    {
      g_assert_not_reached();
    }
}

static inline FilterXDictEntry *
_table_get_entries(FilterXDictTable *table)
{
  return (FilterXDictEntry *) ((gchar *) (table + 1) + _table_index_size(table));
}

static inline FilterXDictEntry *
_table_get_entry(FilterXDictTable *table, FilterXDictEntrySlot index)
{
  FilterXDictEntry *entries = _table_get_entries(table);
  return &entries[index];
}

static inline guint
_table_hash_key(FilterXObject *key)
{
  return filterx_string_hash(key);
}

static inline gboolean
_table_key_equals(FilterXObject *key1, FilterXObject *key2)
{
  if (key1 == key2)
    return TRUE;

  const gchar *key1_str, *key2_str;
  gsize key1_len, key2_len;

  if (!filterx_object_extract_string_ref(key1, &key1_str, &key1_len))
    g_assert_not_reached();
  if (!filterx_object_extract_string_ref(key2, &key2_str, &key2_len))
    g_assert_not_reached();

  if (key1_len != key2_len)
    return FALSE;

  return memcmp(key1_str, key2_str, key1_len) == 0;
}

#define PERTURB_SHIFT 5

static gboolean
_table_lookup_index_slot(FilterXDictTable *table, FilterXObject *key, guint hash, FilterXDictIndexSlot *index_slot)
{
  FilterXDictIndexSlot slot = hash & table->mask;
  FilterXDictIndexSlot perturb = hash;
  FilterXDictIndexSlot first_dummy_slot = FILTERX_DICT_INDEX_SLOT_NONE;

  for (guint32 i = 0; i < table->size; i++)
    {
      FilterXDictEntrySlot entry_slot = _table_get_index_entry(table, slot);
      if (entry_slot == FXD_IX_DUMMY && first_dummy_slot == FILTERX_DICT_INDEX_SLOT_NONE)
        {
          /* we should reuse the dummy slot instead of creating a new at the first empty. */
          first_dummy_slot = slot;
        }
      if (entry_slot == FXD_IX_EMPTY)
        {
          /* we found an empty slot, e.g.  we did not find the key but we
           * found where it could be inserted */
          *index_slot = (first_dummy_slot != FILTERX_DICT_INDEX_SLOT_NONE) ? first_dummy_slot : slot;
          return FALSE;
        }
      else if (entry_slot >= 0)
        {
          FilterXDictEntry *entry = _table_get_entry(table, entry_slot);
          if (entry->key == key)
            {
              *index_slot = slot;
              return TRUE;
            }
          if (_table_hash_key(entry->key) == hash)
            {
              /* hash matches, so it's either a hash collision or a match */
              if (_table_key_equals(entry->key, key))
                {
                  /* it's a match! */
                  *index_slot = slot;
                  return TRUE;
                }
              /* hash collision, let's move to the next item */
            }
        }
      perturb >>= PERTURB_SHIFT;
      /* move 5 slots away in a step. We will eventually visit all possible slots. */
      slot = ((5 * slot) + perturb + 1) & table->mask;

    }

  /* worst case scenario: we only find an empty slot linearly, but since we
   * are only filling the hash table 2/3 of its capacity, we have to find an
   * empty slot before the end of the for loop above */

  g_assert_not_reached();
}

/* NOTE: consumes refs of both key/value */
static void
_table_insert(FilterXDictTable *table, FilterXObject *key, FilterXObject *value)
{
  guint hash = _table_hash_key(key);

  FilterXDictIndexSlot index_slot;
  FilterXDictEntrySlot entry_slot;
  FilterXDictEntry *entry;

  if (_table_lookup_index_slot(table, key, hash, &index_slot))
    {
      /* found, override old entry */
      entry_slot = _table_get_index_entry(table, index_slot);
      entry = _table_get_entry(table, entry_slot);
      filterx_dict_entry_clear(entry);
    }
  else
    {
      /* not found, create new entry, use the index entry identified by index_slot */
      entry_slot = table->entries_num++;
      entry = _table_get_entry(table, entry_slot);
      _table_set_index_entry(table, index_slot, entry_slot);
    }
  /* entry is not zero initialized, make sure you will all fields */
  entry->key = key;
  entry->value = value;
}

static gboolean
_table_lookup(FilterXDictTable *table, FilterXObject *key, FilterXObject **value)
{
  guint hash = _table_hash_key(key);

  FilterXDictIndexSlot index_slot;
  FilterXDictEntrySlot entry_slot;
  FilterXDictEntry *entry;

  if (!_table_lookup_index_slot(table, key, hash, &index_slot))
    return FALSE;

  entry_slot = _table_get_index_entry(table, index_slot);
  entry = _table_get_entry(table, entry_slot);
  *value = filterx_object_ref(entry->value);
  return TRUE;
}

static gboolean
_table_unset(FilterXDictTable *table, FilterXObject *key)
{
  guint hash = _table_hash_key(key);

  FilterXDictIndexSlot index_slot;
  FilterXDictEntrySlot entry_slot;
  FilterXDictEntry *entry;

  if (!_table_lookup_index_slot(table, key, hash, &index_slot))
    return TRUE;

  entry_slot = _table_get_index_entry(table, index_slot);
  entry = _table_get_entry(table, entry_slot);
  filterx_dict_entry_clear(entry);
  _table_set_index_entry(table, index_slot, FXD_IX_DUMMY);
  table->empty_num++;
  return TRUE;
}

static gboolean
_table_foreach(FilterXDictTable *table, FilterXDictForeachFunc func, gpointer user_data)
{
  for (FilterXDictEntrySlot i = 0; i < table->entries_num; i++)
    {
      FilterXDictEntry *entry = _table_get_entry(table, i);

      if (!entry->key)
        continue;

      if (!func(&entry->key, &entry->value, user_data))
        return FALSE;
    }
  return TRUE;
}

static gsize
_table_size(FilterXDictTable *table)
{
  return table->entries_num - table->empty_num;
}

static FilterXDictTable *
_table_new(gsize initial_size)
{
  gint table_size_log2 = _round_to_log2(initial_size);
  gsize table_size = _pow2(table_size_log2);
  gsize entries_size = _table_usable_entries(table_size);

  FilterXDictTable *table = g_malloc(sizeof(FilterXDictTable) +
                                     table_size * _table_index_element_size(table_size) +
                                     entries_size * sizeof(FilterXDictEntry));
  table->size_log2 = table_size_log2;
  table->size = table_size;
  table->entries_size = entries_size;
  table->entries_num = 0;
  table->mask = (1 << table_size_log2) - 1;
  table->empty_num = 0;

  /* this should set all elements to -1, regardless of element size, at
   * least on computers with 2 complements representation of binary numbers
   * */

  memset(&table->indices, -1, table_size * _table_index_element_size(table_size));
  return table;
}

static void
_table_resize_index(FilterXDictTable *target, FilterXDictTable *source)
{
  FilterXDictEntrySlot max = target->entries_num;
  FilterXDictEntry *ep = _table_get_entries(target);
  guint32 mask = target->mask;

  /* iterate over all entries */
  for (FilterXDictEntrySlot entry_slot = 0; entry_slot < max; entry_slot++, ep++)
    {
      if (!ep->key)
        continue;

      guint32 hash = filterx_string_hash(ep->key);
      FilterXDictIndexSlot slot = hash & mask;
      FilterXDictIndexSlot perturb = hash;

      /* find index slot */
      while (_table_get_index_entry(target, slot) >= 0)
        {
          perturb >>= PERTURB_SHIFT;
          slot = (slot * 5 + perturb + 1) & mask;
        }
      _table_set_index_entry(target, slot, entry_slot);
    }
}

static void
_table_resize(FilterXDictTable *target, FilterXDictTable *source)
{
  g_assert(target->size >= source->size);

  target->entries_num = source->entries_num;
  target->empty_num = source->empty_num;
  memcpy(_table_get_entries(target), _table_get_entries(source), source->entries_size * sizeof(FilterXDictEntry));

  _table_resize_index(target, source);
}

static void
_table_clone_index(FilterXDictTable *target, FilterXDictTable *source, FilterXObject *container,
                   FilterXObject *child_of_interest)
{
  memcpy(&target->indices, &source->indices, target->size * _table_index_element_size(target->size));

  FilterXDictEntrySlot max = target->entries_num;
  FilterXDictEntry *ep = _table_get_entries(target);
  gboolean child_found = FALSE;

  /* iterate over all entries */
  for (FilterXDictEntrySlot entry_slot = 0; entry_slot < max; entry_slot++, ep++)
    {
      if (!ep->key)
        continue;

      ep->key = filterx_object_clone(ep->key);
      if (child_of_interest && filterx_ref_values_equal(ep->value, child_of_interest))
        {
          ep->value = filterx_object_ref(child_of_interest);
          child_found = TRUE;
        }
      else
        ep->value = filterx_object_clone(ep->value);
      filterx_ref_set_parent_container(ep->value, container);
    }
  g_assert(child_found || child_of_interest == NULL);
}

static void
_table_clone(FilterXDictTable *target, FilterXDictTable *source, FilterXObject *container,
             FilterXObject *child_of_interest)
{
  g_assert(target->size == source->size);

  target->entries_num = source->entries_num;
  target->empty_num = source->empty_num;
  memcpy(_table_get_entries(target), _table_get_entries(source), source->entries_size * sizeof(FilterXDictEntry));

  _table_clone_index(target, source, container, child_of_interest);
}

static void
_table_free(FilterXDictTable *table, gboolean free_entries)
{
  if (free_entries)
    {
      for (gsize i = 0; i < table->entries_num; i++)
        {
          FilterXDictEntry *entry = _table_get_entry(table, i);
          filterx_dict_entry_clear(entry);
        }
    }
  g_free(table);
}

static FilterXDictTable *
_table_resize_if_needed(FilterXDictTable *old_table)
{
  if (old_table->entries_num < old_table->entries_size)
    return old_table;

  FilterXDictTable *new_table = _table_new(old_table->size * 2);
  _table_resize(new_table, old_table);
  _table_free(old_table, FALSE);
  return new_table;
}

typedef struct _FilterXDictObject
{
  FilterXDict super;
  FilterXDictTable *table;
} FilterXDictObject;

static gboolean
_filterx_dict_truthy(FilterXObject *s)
{
  return TRUE;
}

static gboolean
_filterx_dict_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  *t = LM_VT_JSON;
  return filterx_object_to_json(s, repr);
}

static gboolean
_filterx_dict_repr(FilterXObject *s, GString *repr)
{
  return filterx_object_to_json(s, repr);
}

static inline gboolean
_is_string(FilterXObject *o)
{
  return filterx_object_is_type(o, &FILTERX_TYPE_NAME(string))
         || (filterx_object_is_type(o, &FILTERX_TYPE_NAME(message_value))
             && filterx_message_value_get_type(o) == LM_VT_STRING);
}

static FilterXObject *
_filterx_dict_get_subscript(FilterXDict *s, FilterXObject *key)
{
  FilterXDictObject *self = (FilterXDictObject *) s;

  if (!_is_string(key))
    {
      return NULL;
    }

  FilterXObject *value = NULL;
  if (!_table_lookup(self->table, key, &value))
    return NULL;
  return value;
}

static gboolean
_filterx_dict_set_subscript(FilterXDict *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXDictObject *self = (FilterXDictObject *) s;

  if (!_is_string(key))
    return FALSE;

  self->table = _table_resize_if_needed(self->table);
  _table_insert(self->table, filterx_object_ref(key), filterx_object_cow_store(new_value));

  return TRUE;
}

static gboolean
_filterx_dict_unset_key(FilterXDict *s, FilterXObject *key)
{
  FilterXDictObject *self = (FilterXDictObject *) s;

  if (!_is_string(key))
    return FALSE;

  return _table_unset(self->table, key);
}

static guint64
_filterx_dict_len(FilterXDict *s)
{
  FilterXDictObject *self = (FilterXDictObject *) s;

  return _table_size(self->table);
}

static gboolean
_filterx_dict_foreach_inner(FilterXObject **key, FilterXObject **value, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  FilterXDictIterFunc func = args[0];
  gpointer func_data = args[1];

  return func(*key, *value, func_data);
}

static gboolean
_filterx_dict_iter(FilterXDict *s, FilterXDictIterFunc func, gpointer user_data)
{
  FilterXDictObject *self = (FilterXDictObject *) s;

  gpointer args[] = { func, user_data };
  return _table_foreach(self->table, _filterx_dict_foreach_inner, args);
}

static FilterXObject *
_filterx_dict_factory(FilterXObject *self)
{
  return filterx_dict_new();
}

static FilterXObject *
_filterx_list_factory(FilterXObject *self)
{
  return filterx_list_new();
}

static FilterXObject *
filterx_dict_new_with_table(FilterXDictTable *table)
{
  FilterXDictObject *self = g_new0(FilterXDictObject, 1);

  filterx_dict_init_instance(&self->super, &FILTERX_TYPE_NAME(dict_object));
  self->table = table;
  self->super.get_subscript = _filterx_dict_get_subscript;
  self->super.set_subscript = _filterx_dict_set_subscript;
  self->super.unset_key = _filterx_dict_unset_key;
  self->super.len = _filterx_dict_len;
  self->super.iter = _filterx_dict_iter;
  return &self->super.super;
}

static FilterXObject *
_filterx_dict_clone_container(FilterXObject *s, FilterXObject *container, FilterXObject *child_of_interest)
{
  FilterXDictObject *self = (FilterXDictObject *) s;
  FilterXDictTable *new_table = _table_new(self->table->size);

  _table_clone(new_table, self->table, container, child_of_interest);
  FilterXObject *clone = filterx_dict_new_with_table(new_table);
  return clone;
}

static FilterXObject *
_filterx_dict_clone(FilterXObject *s)
{
  return _filterx_dict_clone_container(s, NULL, NULL);
}

FilterXObject *
filterx_dict_new(void)
{
  return filterx_dict_new_with_table(_table_new(32));
}

FilterXObject *
filterx_dict_sized_new(gsize init_size)
{
  if (init_size < 16)
    init_size = 16;
  return filterx_dict_new_with_table(_table_new(init_size));
}

static void
_filterx_dict_free(FilterXObject *s)
{
  FilterXDictObject *self = (FilterXDictObject *) s;

  _table_free(self->table, TRUE);
  filterx_object_free_method(s);
}

FilterXObject *
filterx_dict_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (args_len == 0)
    return filterx_dict_new();

  if (args_len != 1)
    {
      filterx_eval_push_error("Too many arguments", s, NULL);
      return NULL;
    }

  FilterXObject *arg = args[0];

  FilterXObject *arg_unwrapped = filterx_ref_unwrap_ro(arg);
  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(dict_object)))
    return filterx_object_ref(arg);

  if (filterx_object_is_type(arg_unwrapped, &FILTERX_TYPE_NAME(dict)))
    {
      FilterXObject *self = filterx_dict_new();
      if (!filterx_dict_merge(self, arg_unwrapped))
        {
          filterx_object_unref(self);
          return NULL;
        }
      return self;
    }

  const gchar *repr;
  gsize repr_len;
  repr = filterx_string_get_value_ref(arg, &repr_len);
  if (repr)
    {
      GError *error = NULL;
      FilterXObject *self = filterx_object_from_json(repr, repr_len, &error);
      if (!self)
        {
          filterx_eval_push_error_info("Argument must be a valid JSON string", s,
                                       g_strdup(error->message), TRUE);
          g_clear_error(&error);
          return NULL;
        }
      if (!filterx_object_is_type_or_ref(self, &FILTERX_TYPE_NAME(dict)))
        {
          filterx_object_unref(self);
          return NULL;
        }
      return self;
    }

  filterx_eval_push_error_info("Argument must be a dict or a string", s,
                               g_strdup_printf("got \"%s\" instead", arg_unwrapped->type->name), TRUE);
  return NULL;
}

FILTERX_DEFINE_TYPE(dict_object, FILTERX_TYPE_NAME(dict),
                    .is_mutable = TRUE,
                    .truthy = _filterx_dict_truthy,
                    .free_fn = _filterx_dict_free,
                    .dict_factory = _filterx_dict_factory,
                    .list_factory = _filterx_list_factory,
                    .marshal = _filterx_dict_marshal,
                    .repr = _filterx_dict_repr,
                    .clone = _filterx_dict_clone,
                    .clone_container = _filterx_dict_clone_container,
                   );

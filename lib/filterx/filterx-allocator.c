/*
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
#include "filterx-allocator.h"
#include "tls-support.h"
#include "compat/pow2.h"
#include "messages.h"

/*
 * The allocator provides storage for FilterXObject instances that is:
 *
 *   - thread specific: no locking is needed for allocating or freeing
 *
 *   - free is a no-op: all objects are freed in a single operation at the
 *     end of the filterx block
 *
 *   - tightly packed: all objects reside close to each other
 *
 * The allocator is managed by FilterXEvalContext, but is also retained in a
 * thread-specific variable.  This means that the same thread will always
 * reuse the same allocator.
 *
 * There's a strict ordering between allocations: allocs that happen later
 * always follow those that were made earlier.  Earlier allocations can
 * never reference newer ones, only the other way around: new allocations
 * can reference old values.
 *
 * This property means that we can "reset" the allocator to go back to a
 * specific state and free everything that was allocated later, if we know
 * that all references outside of the allocator go out of scope.
 *
 * This property matches nicely with the way filterx code is executed.
 * FilterX blocks have a specific set of variables and once the block
 * terminates the variables can be freed.  This is exactly how this
 * allocator is intended to be used.
 *
 * filterx_allocator_save_position() - returns the current position
 *
 * filterx_allocator_restore_position() - resets the position to a
 * previously saved state
 *
 */

/* one chunk of memory we allocate at once */
#define FILTERX_AREA_SIZE 65536

typedef struct _FilterXArea
{
  gsize size, used;
  gchar mem[] __attribute__ ((aligned (16)));
} FilterXArea;

#define ALIGN_SIZE(x) (((x) + 0xF) & ~0xF)

static gpointer
filterx_area_alloc(FilterXArea *self, gsize new_size)
{
  gsize alloc_size = ALIGN_SIZE(new_size);

  /* no more space here */
  if (self->size < self->used + alloc_size)
    return NULL;

  gpointer res = &self->mem[self->used];
  self->used += alloc_size;
  return res;
}

static void
filterx_area_reset(FilterXArea *self, gsize pos)
{
#if SYSLOG_NG_ENABLE_DEBUG
  memset(&self->mem[pos], '`', self->size - pos);
#endif
  self->used = pos;
}

#if SYSLOG_NG_ENABLE_DEBUG
static void
filterx_area_shrink_for_debug_purposes(FilterXArea *self, gsize pos)
{
  filterx_area_reset(self, pos);

  /* we behave as if this area was smaller, so that no further allocation is
   * made from this area */
  self->size = pos + sizeof(*self);
}
#endif

static FilterXArea *
filterx_area_new(gsize size)
{
  g_assert(size > sizeof(FilterXArea));
  FilterXArea *self = g_malloc(size);
  memset(self, 0, sizeof(*self));
  self->size = size - sizeof(*self);
  return self;
}

void
filterx_area_free(FilterXArea *self)
{
  g_free(self);
}

/* per thread state */

static FilterXArea *
_create_new_area(FilterXAllocator *allocator)
{
  FilterXArea *area = filterx_area_new(FILTERX_AREA_SIZE);
  g_ptr_array_add(allocator->areas, area);
  return area;
}

gpointer
filterx_allocator_malloc(FilterXAllocator *allocator, gsize size, gsize zero_size)
{
  FilterXArea *area;

  g_assert(filterx_allocator_alloc_size_supported(allocator, size));
  if (allocator->areas->len == 0)
    {
      area = _create_new_area(allocator);
      allocator->active_area = 0;
    }
  else
    {
      area = g_ptr_array_index(allocator->areas, allocator->active_area);
    }
  gpointer res = filterx_area_alloc(area, size);
  if (!res)
    {
      allocator->active_area++;
      if (allocator->active_area == allocator->areas->len)
        {
          area = _create_new_area(allocator);
        }
      else
        {
          area = g_ptr_array_index(allocator->areas, allocator->active_area);
          filterx_area_reset(area, 0);
        }
      res = filterx_area_alloc(area, size);

      /* the allocation size MUST always fit into FILTERX_AREA_SIZE, thus we
       * can never get NULLs from a fresh area.  Callers should always guard
       * filterx_allocator_alloc() calls with
       * filterx_allocator_alloc_size_supported(). */
      g_assert(res != NULL);
    }
  memset(res, 0, ALIGN_SIZE(zero_size));
  return res;
}


/* save the current allocator position, so we can restore it when we are
 * finished with the current filterx block */
void
filterx_allocator_save_position(FilterXAllocator *allocator, FilterXAllocatorPosition *pos)
{
  if (allocator->areas->len > 0)
    {
      pos->area = allocator->active_area;
      if (allocator->areas->len == pos->area)
        {
          /* we are at the end of our allocated areas, let's save the last
           * real one */
          pos->area--;
        }
      FilterXArea *area = g_ptr_array_index(allocator->areas, pos->area);
      pos->area_used = area->used;
    }
  else
    {
      pos->area = -1;
    }
  msg_debug("Saving FilterX allocator position",
            evt_tag_int("position_index", allocator->position_index),
            evt_tag_int("area", pos->area),
            evt_tag_int("used", pos->area_used));
  pos->position_index = allocator->position_index++;
}

#if SYSLOG_NG_ENABLE_DEBUG
static void
filterx_allocator_shrink_for_debug_purposes(FilterXAllocator *allocator, FilterXAllocatorPosition *pos)
{
  /* free all the areas past the current one */
  g_ptr_array_set_size(allocator->areas, pos->area + 1);
  FilterXArea *area = g_ptr_array_index(allocator->areas, pos->area);
  filterx_area_shrink_for_debug_purposes(area, pos->area_used);
}
#endif

/* restore the allocator position to the previous one.  This can only be
 * restored in the same order */
void
filterx_allocator_restore_position(FilterXAllocator *allocator, FilterXAllocatorPosition *pos)
{
  FilterXAllocatorPosition pos_zero = { 0 };
  g_assert(allocator->position_index == pos->position_index + 1);
  allocator->position_index--;

  if (allocator->areas->len > 0)
    {
      msg_debug("Restoring FilterX allocator position",
                evt_tag_int("position_index", pos->position_index),
                evt_tag_int("area", pos->area),
                evt_tag_int("used", pos->area_used));
      if (pos->area < 0)
        pos = &pos_zero;

#if !SYSLOG_NG_ENABLE_DEBUG
      FilterXArea *area = g_ptr_array_index(allocator->areas, pos->area);
      filterx_area_reset(area, pos->area_used);
#else
      filterx_allocator_shrink_for_debug_purposes(allocator, pos);
#endif
      allocator->active_area = pos->area;
    }
}

void
filterx_allocator_empty(FilterXAllocator *allocator)
{
  if (allocator->areas->len > 0)
    {
      FilterXArea *area;

      area = g_ptr_array_index(allocator->areas, 0);
      msg_debug("Emptying FilterX allocator",
                evt_tag_int("areas", allocator->areas->len),
                evt_tag_int("area", allocator->active_area),
                evt_tag_int("used", area->used));
      filterx_area_reset(area, 0);
    }
  allocator->active_area = 0;
#if SYSLOG_NG_ENABLE_DEBUG
  g_ptr_array_set_size(allocator->areas, 0);
#endif
}

void
filterx_allocator_init(FilterXAllocator *allocator)
{
  if (!allocator->areas)
    {
      allocator->areas = g_ptr_array_new_full(16, (GDestroyNotify) filterx_area_free);
      allocator->active_area = 0;
    }
  else
    {
      g_assert(allocator->active_area == 0);
    }
}

void
filterx_allocator_clear(FilterXAllocator *allocator)
{
  if (allocator->areas)
    g_ptr_array_free(allocator->areas, TRUE);
  allocator->areas = NULL;
}

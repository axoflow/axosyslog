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

#include <criterion/criterion.h>

#include "filterx/filterx-allocator.h"
#include "apphook.h"

static gboolean
_within_area(FilterXAllocator *allocator, guint area_index, gpointer p)
{
  /* the area header is private to the allocator, but an area is one block:
   * its payload follows the header, so a pointer into the block is enough */
  FilterXArea *area = g_ptr_array_index(allocator->areas, area_index);
  gchar *ptr = p;
  return ptr >= area->mem && ptr < &area->mem[area->used];
}

Test(filterx_allocator, thread_arena_gets_the_unregistered_thread_id_and_default_areas)
{
  FilterXAllocator allocator = { 0 };

  filterx_allocator_init(&allocator);
  /* this test thread is not a main loop worker */
  cr_assert_eq(allocator.id, 1);
  cr_assert_eq(allocator.next_area_size, FILTERX_ALLOCATOR_DEFAULT_AREA_SIZE);

  /* idempotent, as before */
  filterx_allocator_init(&allocator);
  cr_assert_eq(allocator.id, 1);

  filterx_allocator_clear(&allocator);
}

Test(filterx_allocator, private_arena_starts_small_and_doubles)
{
  FilterXAllocator allocator = { 0 };

  filterx_allocator_init_ext(&allocator, FILTERX_ALLOCATOR_ID_PRIVATE, 256);
  cr_assert_eq(allocator.id, FILTERX_ALLOCATOR_ID_PRIVATE);

  /* 256 bytes minus the area header holds two 100 byte allocations (aligned
   * to 112), the third opens a second, twice as large area */
  gpointer a = filterx_allocator_malloc(&allocator, 100, 100);
  gpointer b = filterx_allocator_malloc(&allocator, 100, 100);
  cr_assert_eq(allocator.areas->len, 1);
  cr_assert(_within_area(&allocator, 0, a));
  cr_assert(_within_area(&allocator, 0, b));

  gpointer c = filterx_allocator_malloc(&allocator, 100, 100);
  cr_assert_eq(allocator.areas->len, 2);
  cr_assert(_within_area(&allocator, 1, c));
  cr_assert_eq(allocator.next_area_size, 1024);

  filterx_allocator_clear(&allocator);
}

Test(filterx_allocator, area_grows_to_fit_an_allocation_larger_than_the_next_area)
{
  FilterXAllocator allocator = { 0 };

  filterx_allocator_init_ext(&allocator, FILTERX_ALLOCATOR_ID_PRIVATE, 256);

  /* the maximum supported size straight away: not 256, not 512, but an area
   * that holds it, and the sequence continues from there */
  gpointer a = filterx_allocator_malloc(&allocator, FILTERX_ALLOCATOR_MAX_ALLOC_SIZE, 16);
  cr_assert_not_null(a);
  cr_assert_eq(allocator.areas->len, 1);
  cr_assert_geq(allocator.next_area_size, 2 * FILTERX_ALLOCATOR_MAX_ALLOC_SIZE);
  cr_assert_leq(allocator.next_area_size, FILTERX_ALLOCATOR_DEFAULT_AREA_SIZE);

  filterx_allocator_clear(&allocator);
}

Test(filterx_allocator, position_restore_reuses_kept_areas_and_skips_the_too_small_ones)
{
  FilterXAllocator allocator = { 0 };
  FilterXAllocatorPosition pos;

  filterx_allocator_init_ext(&allocator, FILTERX_ALLOCATOR_ID_PRIVATE, 256);
  filterx_allocator_save_position(&allocator, &pos);

  /* three areas: 256, 512 and 1024 bytes */
  filterx_allocator_malloc(&allocator, 200, 16);
  filterx_allocator_malloc(&allocator, 400, 16);
  filterx_allocator_malloc(&allocator, 900, 16);
  cr_assert_eq(allocator.areas->len, 3);

  filterx_allocator_restore_position(&allocator, &pos);

  gpointer p = filterx_allocator_malloc(&allocator, 900, 16);
#if SYSLOG_NG_ENABLE_DEBUG
  /* a debug build drops the areas past the restored position (so that a
   * stale pointer into them is caught), leaving nothing to reuse: the 900
   * byte request gets a new, large enough area */
  cr_assert_eq(allocator.areas->len, 2);
  cr_assert(_within_area(&allocator, 1, p));
  cr_assert_eq(allocator.active_area, 1);
#else
  /* back at the first area; the 900 byte request does not fit the first
   * two kept areas and lands in the third, without creating a new one */
  cr_assert_eq(allocator.areas->len, 3);
  cr_assert(_within_area(&allocator, 2, p));
  cr_assert_eq(allocator.active_area, 2);
#endif

  filterx_allocator_clear(&allocator);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(filterx_allocator, .init = setup, .fini = teardown);

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

#include <criterion/criterion.h>
#include "filterx/filterx-plist.h"
#include "apphook.h"
#include "scratch-buffers.h"

#include "libtest/cr_template.h"
#include "libtest/filterx-lib.h"

static void
_destroy_ptr(gpointer v)
{
}

Test(filterx_plist, test_init_and_clear)
{
  FilterXPointerList plist;

  filterx_pointer_list_init(&plist);
  filterx_pointer_list_clear(&plist, _destroy_ptr);
}

Test(filterx_plist, test_elements_can_be_added_and_iterated_in_unsealed_mode)
{
  FilterXPointerList plist;
  gint i;

  filterx_pointer_list_init(&plist);
  for (i = 0; i < 10; i++)
    {
      filterx_pointer_list_add(&plist, GUINT_TO_POINTER(i));
    }

  cr_assert(filterx_pointer_list_get_length(&plist) == i);
  for (i = 0; i < 10; i++)
    {
      gpointer p = filterx_pointer_list_index(&plist, i);
      cr_assert_eq(p, GUINT_TO_POINTER(i));
    }
  filterx_pointer_list_clear(&plist, _destroy_ptr);
}

Test(filterx_plist, test_elements_can_be_added_and_iterated_in_sealed_mode)
{
  FilterXPointerList plist;
  gint i;

  filterx_pointer_list_init(&plist);
  for (i = 0; i < 10; i++)
    {
      filterx_pointer_list_add(&plist, GUINT_TO_POINTER(i));
    }

  filterx_pointer_list_seal(&plist);
  cr_assert(filterx_pointer_list_get_length(&plist) == i);
  for (i = 0; i < 10; i++)
    {
      gpointer p = filterx_pointer_list_index(&plist, i);
      cr_assert_eq(p, GUINT_TO_POINTER(i));
    }
  filterx_pointer_list_clear(&plist, _destroy_ptr);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}


TestSuite(filterx_plist, .init = setup, .fini = teardown);

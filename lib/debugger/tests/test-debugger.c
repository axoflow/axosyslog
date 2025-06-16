/*
 * Copyright (c) 2015 Balabit
 * Copyright (c) 2015 Balázs Scheidler <bazsi@balabit.hu>
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

#include "debugger/debugger.h"
#include "apphook.h"

void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
}

void
teardown(void)
{
  cfg_free(configuration);
}

TestSuite(debugger, .init = setup, .fini = teardown);

Test(debugger, test_debugger)
{
  MainLoop *main_loop = main_loop_get_instance();
  Debugger *debugger = debugger_new(main_loop, configuration);
  debugger_free(debugger);
}


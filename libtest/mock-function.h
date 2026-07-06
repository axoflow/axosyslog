/*
 * Copyright (c) 2026 Balázs Scheidler <balazs.scheidler@axoflow.com>
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
#ifndef LIBTEST_MOCK_FUNCTION_H_INCLUDED
#define LIBTEST_MOCK_FUNCTION_H_INCLUDED

#include "syslog-ng.h"

#if SYSLOG_NG_ENABLE_BUILTIN_MODULES

#define MOCK_FUNCTION(fn)  __wrap_##fn

#else

#define MOCK_FUNCTION(fn)  fn

#endif

#endif

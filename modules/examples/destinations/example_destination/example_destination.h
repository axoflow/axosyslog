/*
 * Copyright (c) 2020 Balabit
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

#ifndef EXAMPLE_DESTINATION_H_INCLUDED
#define EXAMPLE_DESTINATION_H_INCLUDED

#include "driver.h"
#include "logthrdest/logthrdestdrv.h"

typedef struct
{
  LogThreadedDestDriver super;
  GString *filename;
} ExampleDestinationDriver;

LogDriver *example_destination_dd_new(GlobalConfig *cfg);

void example_destination_dd_set_filename(LogDriver *d, const gchar *filename);

#endif

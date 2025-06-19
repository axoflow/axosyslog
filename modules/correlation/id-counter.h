/*
 * Copyright (c) 2023 OneIdentity LLC.
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

#ifndef ID_COUNTER_H_INCLUDED
#define ID_COUNTER_H_INCLUDED

#include "glib.h"

typedef void IdCounter;

IdCounter *id_counter_new(void);
guint id_counter_get_next_id(IdCounter *self);
IdCounter *id_counter_ref(IdCounter *self);
void id_counter_unref(IdCounter *self);

#endif

/*
 * Copyright (c) 2020 One Identity
 * Copyright (c) 2020 László Várady
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

#ifndef _SNG_PYTHON_ACK_TRACKER_H
#define _SNG_PYTHON_ACK_TRACKER_H

#include "python-module.h"
#include "ack-tracker/ack_tracker_factory.h"

typedef struct _PyAckTrackerFactory
{
  PyObject_HEAD
  AckTrackerFactory *ack_tracker_factory;
  PyObject *ack_callback;
} PyAckTrackerFactory;

extern PyTypeObject py_ack_tracker_factory_type;
extern PyTypeObject py_instant_ack_tracker_factory_type;
extern PyTypeObject py_consecutive_ack_tracker_factory_type;
extern PyTypeObject py_batched_ack_tracker_factory_type;

int py_is_ack_tracker_factory(PyObject *obj);

void py_ack_tracker_global_init(void);

#endif

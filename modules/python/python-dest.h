/*
 * Copyright (c) 2014-2015 Balabit
 * Copyright (c) 2014 Gergely Nagy <algernon@balabit.hu>
 * Copyright (c) 2014-2015 Balazs Scheidler <balazs.scheidler@balabit.com>
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

#ifndef _SNG_PYTHON_DEST_H
#define _SNG_PYTHON_DEST_H

#include "python-module.h"
#include "python-binding.h"
#include "driver.h"
#include "logwriter.h"
#include "value-pairs/value-pairs.h"

LogDriver *python_dd_new(GlobalConfig *cfg);
void python_dd_set_value_pairs(LogDriver *d, ValuePairs *vp);
PythonBinding *python_dd_get_binding(LogDriver *d);
LogTemplateOptions *python_dd_get_template_options(LogDriver *d);

void py_log_destination_global_init(void);

#endif

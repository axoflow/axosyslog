/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef PUBSUB_DEST_H
#define PUBSUB_DEST_H

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "driver.h"
#include "template/templates.h"

LogDriver *pubsub_dd_new(GlobalConfig *cfg);

void pubsub_dd_set_project(LogDriver *d, LogTemplate *project);
void pubsub_dd_set_topic(LogDriver *d, LogTemplate *topic);
void pubsub_dd_set_data(LogDriver *d, LogTemplate *data);
gboolean pubsub_dd_set_protovar(LogDriver *d, LogTemplate *proto);
void pubsub_dd_add_attribute(LogDriver *d, const gchar *name, LogTemplate *value);

#include "compat/cpp-end.h"

#endif

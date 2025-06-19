/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2024 Attila Szakacs
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

#ifndef XML_PRIVATE_H_INCLUDED
#define XML_PRIVATE_H_INCLUDED

#include "logmsg/logmsg.h"

GString *xml_parser_append_values(const gchar *previous_value, gssize previous_value_len,
                                  const gchar *current_value, gssize current_value_len,
                                  gboolean create_lists, LogMessageValueType *type);

#endif

/*
 * Copyright (c) 2015 BalaBit
 * Copyright (c) 2015 Tibor Benke
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

#ifndef PARSER_H_INCLUDED
#define PARSER_H_INCLUDED

#include "parser/parser-expr.h"

gboolean native_parser_set_option(LogParser *s, gchar *key, gchar *value);
LogParser *native_parser_new(GlobalConfig *cfg);

#endif

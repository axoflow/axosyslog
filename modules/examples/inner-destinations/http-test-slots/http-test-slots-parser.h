/*
 * Copyright (c) 2020 One Identity
 * Copyright (c) 2020 Laszlo Budai <laszlo.budai@outlook.com>
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

#ifndef HTTP_TEST_SLOTS_PARSER_H_INCLUDED
#define HTTP_TEST_SLOTS_PARSER_H_INCLUDED

#include "cfg-parser.h"
#include "driver.h"

extern CfgParser http_test_slots_parser;

CFG_PARSER_DECLARE_LEXER_BINDING(http_test_slots_, HTTP_TEST_SLOTS_, LogDriverPlugin **);

#endif

/*
 * Copyright (c) 2013 Balabit
 * Copyright (c) 2013 Balazs Scheidler <bazsi@balabit.hu>
 * Copyright (c) 2022 Balazs Scheidler <bazsi77@gmail.com>
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

#ifndef MULTI_LINE_REGEXP_MULTI_LINE_H_INCLUDED
#define MULTI_LINE_REGEXP_MULTI_LINE_H_INCLUDED

#include "multi-line/multi-line-logic.h"
#include "multi-line/multi-line-pattern.h"

typedef struct _RegexpMultiLine
{
  MultiLineLogic super;
  enum
  {
    RML_PREFIX_GARBAGE,
    RML_PREFIX_SUFFIX,
  } mode;
  MultiLinePattern *prefix;
  MultiLinePattern *garbage;
} RegexpMultiLine;

MultiLineLogic *regexp_multi_line_new(gint mode, MultiLinePattern *prefix, MultiLinePattern *garbage_or_suffix);

#endif

/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2013 Balázs Scheidler
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

#ifndef FILTER_PIPE_H_INCLUDED
#define FILTER_PIPE_H_INCLUDED

#include "filter/filter-expr.h"
#include "logpipe.h"

/* convert a filter expression into a drop/accept LogPipe */

/*
 * This class encapsulates a LogPipe that either drops/allows a LogMessage
 * to go through.
 */
typedef struct _LogFilterPipe
{
  LogPipe super;
  FilterExprNode *expr;
  gchar *name;
  StatsCounterItem *matched;
  StatsCounterItem *not_matched;
} LogFilterPipe;

LogPipe *log_filter_pipe_new(FilterExprNode *expr, GlobalConfig *cfg);

#endif

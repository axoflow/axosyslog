/*
 * Copyright (c) 2023 Balázs Scheidler <balazs.scheidler@axoflow.com>
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
#ifndef LOGSCHEDULER_PIPE_H_INCLUDED
#define LOGSCHEDULER_PIPE_H_INCLUDED

#include "logpipe.h"
#include "logscheduler.h"

typedef struct _LogSchedulerPipe
{
  LogPipe super;
  LogSchedulerOptions scheduler_options;
  LogScheduler *scheduler;
} LogSchedulerPipe;

LogSchedulerOptions *log_scheduler_pipe_get_scheduler_options(LogPipe *s);
LogPipe *log_scheduler_pipe_new(GlobalConfig *cfg);

#endif

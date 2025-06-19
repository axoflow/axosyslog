/*
 * Copyright (c) 2015 Balabit
 * Copyright (c) 2015 Balázs Scheidler
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
#ifndef DEBUGGER_DEBUGGER_H_INCLUDED
#define DEBUGGER_DEBUGGER_H_INCLUDED 1

#include "syslog-ng.h"
#include "cfg.h"
#include "mainloop.h"

typedef struct _Debugger Debugger;

typedef gchar *(*FetchCommandFunc)(void);


gchar *debugger_builtin_fetch_command(void);
void debugger_register_command_fetcher(FetchCommandFunc fetcher);
void debugger_exit(Debugger *self);
void debugger_start_console(Debugger *self);
gboolean debugger_perform_tracing(Debugger *self, LogPipe *pipe, LogMessage *msg);
gboolean debugger_stop_at_breakpoint(Debugger *self, LogPipe *pipe, LogMessage *msg);

Debugger *debugger_new(MainLoop *main_loop, GlobalConfig *cfg);
void debugger_free(Debugger *self);


#endif

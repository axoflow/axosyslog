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
#ifndef MAINLOOP_H_INCLUDED
#define MAINLOOP_H_INCLUDED

#include "syslog-ng.h"
#include "thread-utils.h"

extern volatile gint main_loop_workers_running;

typedef struct _MainLoop MainLoop;

typedef struct _MainLoopOptions
{
  gchar *preprocess_into;
  gboolean syntax_only;
  gboolean check_startup;
  gboolean config_id;
  gboolean interactive_mode;
  gboolean disable_module_discovery;
} MainLoopOptions;

extern ThreadId main_thread_handle;
extern GCond thread_halt_cond;
extern GMutex workers_running_lock;

typedef gpointer (*MainLoopTaskFunc)(gpointer user_data);

static inline void
main_loop_assert_main_thread(void)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(threads_equal(main_thread_handle, get_thread_id()));
#endif
}

static inline gboolean
main_loop_is_main_thread(void)
{
  return threads_equal(main_thread_handle, get_thread_id());
}

gboolean main_loop_reload_config_prepare(MainLoop *self, GError **error);
void main_loop_reload_config_commence(MainLoop *self);
void main_loop_reload_config(MainLoop *self);
void main_loop_verify_config(GString *result, MainLoop *self);
gboolean main_loop_is_terminating(MainLoop *self);
void main_loop_exit(MainLoop *self);

int main_loop_read_and_init_config(MainLoop *self);
gboolean main_loop_was_last_reload_successful(MainLoop *self);
void main_loop_run(MainLoop *self);

MainLoop *main_loop_get_instance(void);
GlobalConfig *main_loop_get_current_config(MainLoop *self);
GlobalConfig *main_loop_get_pending_new_config(MainLoop *self);
void main_loop_init(MainLoop *self, MainLoopOptions *options);
void main_loop_deinit(MainLoop *self);

void main_loop_add_options(GOptionContext *ctx);

gboolean main_loop_initialize_state(GlobalConfig *cfg, const gchar *persist_filename);

void main_loop_thread_resource_init(void);
void main_loop_thread_resource_deinit(void);

gboolean main_loop_is_control_server_running(MainLoop *self);

#define MAIN_LOOP_ERROR main_loop_error_quark()

GQuark main_loop_error_quark(void);

enum MainLoopError
{
  MAIN_LOOP_ERROR_FAILED,
  MAIN_LOOP_ERROR_RELOAD_FAILED,
};

#endif

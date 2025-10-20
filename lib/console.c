/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
 * Copyright (c) 2024 Balázs Scheidler <balazs.scheidler@axoflow.com>
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
#include "console.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <syslog.h>

GMutex console_lock;
gboolean using_initial_console = TRUE;
const gchar *console_prefix;
gint initial_console_fds[3];
gint stolen_fds;

/**
 * console_printf:
 * @fmt: format string
 * @...: arguments to @fmt
 *
 * This function sends a message to the client preferring to use the stderr
 * channel as long as it is available and switching to using syslog() if it
 * isn't. Generally the stderr channell will be available in the startup
 * process and in the beginning of the first startup in the
 * supervisor/daemon processes. Later on the stderr fd will be closed and we
 * have to fall back to using the system log.
 **/
void
console_printf(const gchar *fmt, ...)
{
  gchar buf[2048];
  va_list ap;

  va_start(ap, fmt);
  g_vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (console_is_initial())
    fprintf(stderr, "%s: %s\n", console_prefix, buf);
  else
    {
      openlog(console_prefix, LOG_PID, LOG_DAEMON);
      syslog(LOG_CRIT, "%s\n", buf);
      closelog();
    }
}


/* NOTE: this is not synced with any changes and is just an indication whether we already acquired the console */
gboolean
console_is_initial(void)
{
  gboolean result;

  /* the lock only serves a memory barrier but is not a real synchronization */
  g_mutex_lock(&console_lock);
  result = using_initial_console;
  g_mutex_unlock(&console_lock);
  return result;
}

GString *
_get_fn_names(gint fns)
{
  GString *result = g_string_new(NULL);

  if (fns & (1 << STDIN_FILENO))
    g_string_append(result, "stdin");
  if (fns & (1 << STDOUT_FILENO))
    {
      if (result->len > 0)
        g_string_append_c(result, ',');
      g_string_append(result, "stdout");
    }
  if (fns & (1 << STDERR_FILENO))
    {
      if (result->len > 0)
        g_string_append_c(result, ',');
      g_string_append(result, "stderr");
    }

  return result;
}

/* re-acquire a console after startup using an array of fds */
gboolean
console_acquire_from_fds(gint fds[3], gint fds_to_steal)
{
  gboolean result = FALSE;

  g_mutex_lock(&console_lock);
  if (!using_initial_console)
    goto exit;

  GString *stolen_fn_names = _get_fn_names(fds_to_steal);
  gchar *takeover_message_on_old_console = g_strdup_printf("[Console(%s) taken over, no further output here]\n",
                                                           stolen_fn_names->str);
  (void) write(STDOUT_FILENO, takeover_message_on_old_console, strlen(takeover_message_on_old_console));
  g_free(takeover_message_on_old_console);
  g_string_free(stolen_fn_names, TRUE);

  stolen_fds = fds_to_steal;

  if (stolen_fds & (1 << STDIN_FILENO))
    initial_console_fds[0] = dup(STDIN_FILENO);
  if (stolen_fds & (1 << STDOUT_FILENO))
    initial_console_fds[1] = dup(STDOUT_FILENO);
  if (stolen_fds & (1 << STDERR_FILENO))
    initial_console_fds[2] = dup(STDERR_FILENO);

  if (stolen_fds & (1 << STDIN_FILENO))
    dup2(fds[0], STDIN_FILENO);
  if (stolen_fds & (1 << STDOUT_FILENO))
    dup2(fds[1], STDOUT_FILENO);
  if (stolen_fds & (1 << STDERR_FILENO))
    dup2(fds[2], STDERR_FILENO);

  using_initial_console = FALSE;
  result = TRUE;
exit:
  g_mutex_unlock(&console_lock);
  return result;
}

/**
 * console_release:
 *
 * Restore input/output/error. This function is idempotent, can be
 * called any number of times without harm.
 **/
void
console_release(void)
{
  g_mutex_lock(&console_lock);

  if (using_initial_console)
    goto exit;

  if (initial_console_fds[0] > 0)
    {
      dup2(initial_console_fds[0], STDIN_FILENO);
      close(initial_console_fds[0]);
      initial_console_fds[0] = -1;
    }
  if (initial_console_fds[1] > 0)
    {
      dup2(initial_console_fds[1], STDOUT_FILENO);
      close(initial_console_fds[1]);
      initial_console_fds[1] = -1;
    }
  if (initial_console_fds[2] > 0)
    {
      dup2(initial_console_fds[2], STDERR_FILENO);
      close(initial_console_fds[2]);
      initial_console_fds[2] = -1;
    }
  using_initial_console = TRUE;
exit:
  g_mutex_unlock(&console_lock);
}

void
console_global_init(const gchar *console_prefix_)
{
  g_mutex_init(&console_lock);
  console_prefix = console_prefix_;
  initial_console_fds[0] = -1;
  initial_console_fds[1] = -1;
  initial_console_fds[2] = -1;
}

void
console_global_deinit(void)
{
  g_mutex_clear(&console_lock);
}

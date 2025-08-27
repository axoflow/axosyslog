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
#include "debugger/debugger.h"
#include "debugger/tracer.h"
#include "logmsg/logmsg.h"
#include "logpipe.h"
#include "apphook.h"
#include "mainloop.h"
#include "timeutils/misc.h"
#include "compat/time.h"
#include "scratch-buffers.h"
#include "cfg-source.h"

#include <iv_signal.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>

struct _Debugger
{
  Tracer *tracer;
  struct iv_signal sigint;
  MainLoop *main_loop;
  GlobalConfig *cfg;
  GThread *debugger_thread;
  BreakpointSite *breakpoint_site;
  struct timespec last_trace_event;

  /* user interface related state */
  gchar *command_buffer;
  struct
  {
    gchar *filename;
    gint line;
    gint column;
    gint list_start;
  } current_location;
  LogTemplate *display_template;
};

static void
_set_command(Debugger *self, gchar *new_command)
{
  if (self->command_buffer)
    g_free(self->command_buffer);
  self->command_buffer = g_strdup(new_command);
}

static gboolean
_format_nvpair(NVHandle handle,
               const gchar *name,
               const gchar *value, gssize length,
               LogMessageValueType type, gpointer user_data)
{
  if (type == LM_VT_BYTES || type == LM_VT_PROTOBUF)
    {
      printf("%s=", name);
      for (gssize i = 0; i < length; i++)
        {
          const guchar b = (const guchar) value[i];
          printf("%02hhx", b);
        }
      printf("\n");
      return FALSE;
    }
  printf("%s=%.*s\n", name, (gint) length, value);
  return FALSE;
}

static void
_display_msg_details(Debugger *self, LogMessage *msg)
{
  GString *output = g_string_sized_new(128);

  log_msg_values_foreach(msg, _format_nvpair, NULL);
  g_string_truncate(output, 0);
  log_msg_format_tags(msg, output, TRUE);
  printf("TAGS=%s\n", output->str);
  printf("\n");
  g_string_free(output, TRUE);
}

static void
_display_msg_with_template(Debugger *self, LogMessage *msg, LogTemplate *template)
{
  GString *output = g_string_sized_new(128);

  log_template_format(template, msg, &DEFAULT_TEMPLATE_EVAL_OPTIONS, output);
  printf("%s\n", output->str);
  g_string_free(output, TRUE);
}

static gboolean
_display_msg_with_template_string(Debugger *self, LogMessage *msg, const gchar *template_string, GError **error)
{
  LogTemplate *template;

  template = log_template_new(self->cfg, NULL);
  if (!log_template_compile(template, template_string, error))
    {
      return FALSE;
    }
  _display_msg_with_template(self, msg, template);
  log_template_unref(template);
  return TRUE;
}

static void
_set_current_location(Debugger *self, LogExprNode *expr_node)
{
  g_free(self->current_location.filename);
  if (expr_node)
    {
      self->current_location.filename = g_strdup(expr_node->filename);
      self->current_location.line = expr_node->line;
      self->current_location.column = expr_node->column;
      self->current_location.list_start = expr_node->line - 5;
    }
  else
    {
      memset(&self->current_location, 0, sizeof(self->current_location));
    }
}

static void
_display_source_line(Debugger *self)
{
  if (self->current_location.filename)
    cfg_source_print_region(self->current_location.filename, self->current_location.line,
                            self->current_location.column, self->current_location.list_start);
  else
    puts("Unable to list source, no current location set");
}

static gboolean
_cmd_help(Debugger *self, gint argc, gchar *argv[])
{
  if (self->breakpoint_site)
    {
      printf("syslog-ng interactive console\n"
             "Stopped on a breakpoint.\n"
             "The following commands are available:\n\n"
             "  help, h, ?               Display this help\n"
             "  info, i                  Display information about the current execution state\n"
             "  list, l                  Display source code at the current location\n"
             "  continue, c              Continue until the next breakpoint\n"
             "  display                  Set the displayed message template\n"
             "  trace, t                 Display timing information as the message traverses the config\n"
             "  print, p                 Print the current log message\n"
             "  drop, d                  Drop the current message\n"
             "  quit, q                  Tell syslog-ng to exit\n"
            );
    }
  else
    {
      printf("syslog-ng interactive console\n"
             "Stopped on an interrupt.\n"
             "The following commands are available:\n\n"
             "  help, h, ?               Display this help\n"
             "  list, l                  Display source code at the current location\n"
             "  continue, c              Continue until the next breakpoint\n"
             "  quit, q                  Tell syslog-ng to exit\n"
            );
    }
  return TRUE;
}

static gboolean
_cmd_continue(Debugger *self, gint argc, gchar *argv[])
{
  return FALSE;
}

static gboolean
_cmd_print(Debugger *self, gint argc, gchar *argv[])
{
  if (argc == 1)
    _display_msg_details(self, self->breakpoint_site->msg);
  else if (argc == 2)
    {
      GError *error = NULL;
      if (!_display_msg_with_template_string(self, self->breakpoint_site->msg, argv[1], &error))
        {
          printf("print: %s\n", error->message);
          g_clear_error(&error);
        }
    }
  else
    printf("print: expected no arguments or exactly one\n");
  return TRUE;
}

static gboolean
_cmd_display(Debugger *self, gint argc, gchar *argv[])
{
  if (argc == 2)
    {
      GError *error = NULL;
      if (!log_template_compile(self->display_template, argv[1], &error))
        {
          printf("display: Error compiling template: %s\n", error->message);
          g_clear_error(&error);
          return TRUE;
        }
    }
  printf("display: The template is set to: \"%s\"\n", self->display_template->template_str);
  return TRUE;
}

static gboolean
_cmd_drop(Debugger *self, gint argc, gchar *argv[])
{
  self->breakpoint_site->drop = TRUE;
  return FALSE;
}

static gboolean
_cmd_trace(Debugger *self, gint argc, gchar *argv[])
{
  self->breakpoint_site->msg->flags |= LF_STATE_TRACING;
  return FALSE;
}

static gboolean
_cmd_quit(Debugger *self, gint argc, gchar *argv[])
{
  main_loop_exit(self->main_loop);
  if (self->breakpoint_site)
    self->breakpoint_site->drop = TRUE;
  return FALSE;
}

static gboolean
_cmd_info_pipe(Debugger *self, LogPipe *pipe)
{
  gchar buf[1024];

  printf("LogPipe %p at %s\n", pipe, log_expr_node_format_location(pipe->expr_node, buf, sizeof(buf)));
  _display_source_line(self);

  return TRUE;
}

static gboolean
_cmd_info(Debugger *self, gint argc, gchar *argv[])
{
  if (argc >= 2)
    {
      if (strcmp(argv[1], "pipe") == 0)
        return _cmd_info_pipe(self, self->breakpoint_site->pipe);
    }

  printf("info: List of info subcommands\n"
         "info pipe -- display information about the current pipe\n");
  return TRUE;
}

static gboolean
_cmd_list(Debugger *self, gint argc, gchar *argv[])
{
  gint shift = 11;
  if (argc >= 2)
    {
      if (strcmp(argv[1], "+") == 0)
        shift = 11;
      else if (strcmp(argv[1], "-") == 0)
        shift = -11;
      else if (strcmp(argv[1], ".") == 0)
        {
          shift = 0;
          if (self->breakpoint_site)
            _set_current_location(self, self->breakpoint_site->pipe->expr_node);
        }
      else if (isdigit(argv[1][0]))
        {
          gint target_lineno = atoi(argv[1]);
          if (target_lineno <= 0)
            target_lineno = 1;
          self->current_location.list_start = target_lineno;
        }
      /* drop any arguments for repeated execution */
      _set_command(self, "l");
    }
  _display_source_line(self);
  if (shift)
    self->current_location.list_start += shift;
  return TRUE;
}

typedef gboolean (*DebuggerCommandFunc)(Debugger *self, gint argc, gchar *argv[]);

struct
{
  const gchar *name;
  DebuggerCommandFunc command;
  gboolean requires_breakpoint_site;
} command_table[] =
{
  { "help",     _cmd_help },
  { "h",        _cmd_help },
  { "?",        _cmd_help },
  { "continue", _cmd_continue },
  { "c",        _cmd_continue },
  { "print",    _cmd_print, .requires_breakpoint_site = TRUE },
  { "p",        _cmd_print, .requires_breakpoint_site = TRUE },
  { "list",     _cmd_list, },
  { "l",        _cmd_list, },
  { "display",  _cmd_display },
  { "drop",     _cmd_drop, .requires_breakpoint_site = TRUE },
  { "d",        _cmd_drop, .requires_breakpoint_site = TRUE },
  { "quit",     _cmd_quit },
  { "q",        _cmd_quit },
  { "trace",    _cmd_trace, .requires_breakpoint_site = TRUE },
  { "t",        _cmd_trace, .requires_breakpoint_site = TRUE },
  { "info",     _cmd_info, .requires_breakpoint_site = TRUE },
  { "i",        _cmd_info, .requires_breakpoint_site = TRUE },
  { NULL, NULL }
};

gchar *
debugger_builtin_fetch_command(void)
{
  gchar buf[1024];
  gsize len;

  printf("(syslog-ng) ");
  fflush(stdout);
  clearerr(stdin);

  if (!fgets(buf, sizeof(buf), stdin))
    return NULL;

  /* strip NL */
  len = strlen(buf);
  if (buf[len - 1] == '\n')
    {
      buf[len - 1] = 0;
    }
  return g_strdup(buf);
}

FetchCommandFunc fetch_command_func = debugger_builtin_fetch_command;

void
debugger_register_command_fetcher(FetchCommandFunc fetcher)
{
  fetch_command_func = fetcher;
}


static void
_fetch_command(Debugger *self)
{
  gchar *command;

  command = fetch_command_func();
  if (command && strlen(command) > 0)
    _set_command(self, command);
  g_free(command);
}

static gboolean
_handle_command(Debugger *self)
{
  gint argc;
  gchar **argv;
  GError *error = NULL;
  gboolean requires_breakpoint_site = TRUE;
  DebuggerCommandFunc command = NULL;

  if (!g_shell_parse_argv(self->command_buffer ? : "", &argc, &argv, &error))
    {
      printf("%s\n", error->message);
      g_clear_error(&error);
      return TRUE;
    }

  for (gint i = 0; command_table[i].name; i++)
    {
      if (strcmp(command_table[i].name, argv[0]) == 0)
        {
          command = command_table[i].command;
          requires_breakpoint_site = command_table[i].requires_breakpoint_site;
          break;
        }
    }
  if (!command)
    {
      printf("Undefined command %s, try \"help\"\n", argv[0]);
      return TRUE;
    }
  else if (requires_breakpoint_site && self->breakpoint_site == NULL)
    {
      printf("Running in interrupt context, command %s requires pipeline context\n", argv[0]);
      return TRUE;
    }
  gboolean result = command(self, argc, argv);
  g_strfreev(argv);
  return result;
}

static void
_handle_interactive_prompt(Debugger *self)
{
  gchar buf[1024];

  if (self->breakpoint_site)
    {
      LogPipe *current_pipe = self->breakpoint_site->pipe;

      _set_current_location(self, current_pipe->expr_node);
      printf("Breakpoint hit %s\n", log_expr_node_format_location(current_pipe->expr_node, buf, sizeof(buf)));
      _display_source_line(self);
      _display_msg_with_template(self, self->breakpoint_site->msg, self->display_template);
    }
  else
    {
      _set_current_location(self, NULL);
      printf("Stopping on interrupt, message related commands are unavailable...\n");
    }
  while (1)
    {
      _fetch_command(self);

      if (!_handle_command(self))
        break;

    }
  printf("(continuing)\n");
}

static gpointer
_debugger_thread_func(Debugger *self)
{
  app_thread_start();
  printf("Waiting for breakpoint...\n");
  while (1)
    {
      self->breakpoint_site = NULL;
      if (!tracer_wait_for_event(self->tracer, &self->breakpoint_site))
        break;

      _handle_interactive_prompt(self);
      tracer_resume_after_event(self->tracer, self->breakpoint_site);
    }
  scratch_buffers_explicit_gc();
  app_thread_stop();
  return NULL;
}

static void
_interrupt(gpointer user_data)
{
  Debugger *self = (Debugger *) user_data;

  tracer_stop_on_interrupt(self->tracer);
}

void
debugger_start_console(Debugger *self)
{
  main_loop_assert_main_thread();

  IV_SIGNAL_INIT(&self->sigint);
  self->sigint.signum = SIGINT;
  self->sigint.flags = IV_SIGNAL_FLAG_EXCLUSIVE;
  self->sigint.cookie = self;
  self->sigint.handler = _interrupt;
  iv_signal_register(&self->sigint);

  self->debugger_thread = g_thread_new(NULL, (GThreadFunc) _debugger_thread_func, self);
}

gboolean
debugger_stop_at_breakpoint(Debugger *self, LogPipe *pipe_, LogMessage *msg)
{
  BreakpointSite breakpoint_site = {0};
  msg_trace("Debugger: stopping at breakpoint",
            log_pipe_location_tag(pipe_));

  breakpoint_site.msg = log_msg_ref(msg);
  breakpoint_site.pipe = log_pipe_ref(pipe_);
  tracer_stop_on_breakpoint(self->tracer, &breakpoint_site);
  log_msg_unref(breakpoint_site.msg);
  log_pipe_unref(breakpoint_site.pipe);
  return !breakpoint_site.drop;
}

gboolean
debugger_perform_tracing(Debugger *self, LogPipe *pipe_, LogMessage *msg)
{
  struct timespec ts, *prev_ts = &self->last_trace_event;
  gchar buf[1024];

  clock_gettime(CLOCK_MONOTONIC, &ts);

  gint64 diff = prev_ts->tv_sec == 0 ? 0 : timespec_diff_nsec(&ts, prev_ts);
  printf("[%"G_GINT64_FORMAT".%09"G_GINT64_FORMAT" +%"G_GINT64_FORMAT"] Tracing %s\n",
         (gint64)ts.tv_sec, (gint64)ts.tv_nsec, diff,
         log_expr_node_format_location(pipe_->expr_node, buf, sizeof(buf)));
  *prev_ts = ts;
  return TRUE;
}

void
debugger_exit(Debugger *self)
{
  main_loop_assert_main_thread();

  iv_signal_unregister(&self->sigint);
  tracer_cancel(self->tracer);
  g_thread_join(self->debugger_thread);
}

Debugger *
debugger_new(MainLoop *main_loop, GlobalConfig *cfg)
{
  Debugger *self = g_new0(Debugger, 1);

  self->main_loop = main_loop;
  self->tracer = tracer_new(cfg);
  self->cfg = cfg;
  self->display_template = log_template_new(cfg, NULL);
  _set_command(self, "help");
  log_template_compile(self->display_template, "$DATE $HOST $MSGHDR$MSG", NULL);
  return self;
}

void
debugger_free(Debugger *self)
{
  g_free(self->current_location.filename);
  log_template_unref(self->display_template);
  tracer_free(self->tracer);
  g_free(self->command_buffer);
  g_free(self);
}

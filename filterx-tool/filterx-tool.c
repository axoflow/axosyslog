/*
 * Copyright (c) 2026 Axoflow
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

#include "resolved-configurable-paths.h"
#include "mainloop.h"
#include "apphook.h"
#include "messages.h"
#include "cfg.h"

#include <stdio.h>
#include <string.h>

typedef struct _FilterXToolCommand
{
  const gchar *name;
  const gchar *description;
  const gchar *parameter_description;
  const GOptionEntry *options;
  gint (*main)(GlobalConfig *cfg, gint argc, gchar *argv[]);
} FilterXToolCommand;

static const gchar *filterx_tool_intro =
  "AxoSyslog FilterX tool is an experimental utility for working with FilterX code interactively.\n"
  "It is work in progress.";

static FilterXToolCommand commands[] =
{
  { NULL },
};

static GString *
_format_commands(void)
{
  GString *commands_list = g_string_new("Commands:\n");

  for (FilterXToolCommand *command = commands; command->name; command++)
    g_string_append_printf(commands_list, "  %-16s %s\n", command->name, command->description);

  return commands_list;
}

static const gchar *
_take_command_name(gint *argc, gchar ***argv)
{
  for (gint i = 1; i < *argc; i++)
    {
      if ((*argv)[i][0] == '-')
        continue;

      const gchar *name = (*argv)[i];
      memmove(&(*argv)[i], &(*argv)[i + 1], (*argc - i) * sizeof(gchar *));
      (*argc)--;
      return name;
    }

  return NULL;
}

static FilterXToolCommand *
_find_command(const gchar *name)
{
  for (FilterXToolCommand *command = commands; command->name; command++)
    {
      if (strcmp(command->name, name) == 0)
        return command;
    }

  return NULL;
}

static gboolean
_parse_options(GOptionContext *ctx, gint *argc, gchar ***argv)
{
  GError *error = NULL;

  gboolean result = g_option_context_parse(ctx, argc, argv, &error);
  if (!result)
    {
      fprintf(stderr, "Error parsing command line arguments: %s\n", error ? error->message : "Invalid arguments");
      g_clear_error(&error);
    }

  g_option_context_free(ctx);
  return result;
}

static gboolean
_parse_main_options(gint *argc, gchar ***argv)
{
  GOptionContext *ctx = g_option_context_new("[<command>]");
  GString *commands_list = _format_commands();

  g_option_context_set_summary(ctx, filterx_tool_intro);
  g_option_context_set_description(ctx, commands_list->str);
  g_string_free(commands_list, TRUE);

  return _parse_options(ctx, argc, argv);
}

static gboolean
_parse_command_options(FilterXToolCommand *command, gint *argc, gchar ***argv)
{
  GOptionContext *ctx = g_option_context_new(command->parameter_description);

  g_option_context_set_summary(ctx, command->description);
  g_option_context_add_main_entries(ctx, command->options, NULL);

  return _parse_options(ctx, argc, argv);
}

static void
_set_command_prgname(const gchar *bin_name, const gchar *command_name)
{
  gchar *prgname = g_strdup_printf("%s %s", bin_name, command_name);

  g_set_prgname(prgname);
  g_free(prgname);
}

static gint
_run(FilterXToolCommand *command, gint argc, gchar *argv[])
{
  msg_init(TRUE);
  resolved_configurable_paths_init(&resolved_configurable_paths);
  app_startup();
  main_thread_handle = get_thread_id();

  GlobalConfig *cfg = cfg_new(VERSION_VALUE_CURRENT);
  gint result = command->main(cfg, argc, argv);
  cfg_free(cfg);

  app_shutdown();
  return result;
}

int
main(int argc, char *argv[])
{
  const gchar *command_name = _take_command_name(&argc, &argv);

  if (!command_name)
    {
      if (!_parse_main_options(&argc, &argv))
        return 1;

      printf("%s\n", filterx_tool_intro);
      return 0;
    }

  FilterXToolCommand *command = _find_command(command_name);
  if (!command)
    {
      GString *commands_list = _format_commands();

      fprintf(stderr, "Unknown command: %s\n%s", command_name, commands_list->str);
      g_string_free(commands_list, TRUE);
      return 1;
    }

  _set_command_prgname(argv[0], command_name);
  if (!_parse_command_options(command, &argc, &argv))
    return 1;

  return _run(command, argc, argv);
}

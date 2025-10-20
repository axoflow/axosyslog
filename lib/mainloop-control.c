/*
 * Copyright (c) 2002-2018 Balabit
 * Copyright (c) 1998-2018 Balázs Scheidler
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
#include "mainloop.h"
#include "control/control-commands.h"
#include "control/control-connection.h"
#include "messages.h"
#include "cfg.h"
#include "cfg-path.h"
#include "apphook.h"
#include "secret-storage/secret-storage.h"
#include "cfg-graph.h"
#include "logpipe.h"
#include "console.h"
#include "debugger/debugger-main.h"

#include <string.h>
#include <unistd.h>

static gboolean
_control_process_log_level(const gchar *level, GString *result)
{
  if (!level)
    {
      /* query current log level */
      return TRUE;
    }
  gint ll = msg_map_string_to_log_level(level);
  if (ll < 0)
    return FALSE;

  msg_set_log_level(ll);
  return TRUE;
}

static gboolean
_control_process_compat_log_command(const gchar *level, const gchar *onoff, GString *result)
{
  if (!level)
    return FALSE;

  gint ll = msg_map_string_to_log_level(level);
  if (ll < 0)
    return FALSE;

  if (onoff)
    {
      gboolean on = g_str_equal(onoff, "ON");
      if (!on)
        ll = ll - 1;

      msg_set_log_level(ll);
    }
  return TRUE;
}

static void
control_connection_message_log(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  gchar **cmds = g_strsplit(command->str, " ", 3);
  GString *result = g_string_sized_new(128);
  gboolean success;

  if (!cmds[1])
    {
      g_string_assign(result, "FAIL Invalid arguments received");
      goto exit;
    }

  gint orig_log_level = msg_get_log_level();

  if (g_str_equal(cmds[1], "LEVEL"))
    success = _control_process_log_level(cmds[2], result);
  else
    success = _control_process_compat_log_command(cmds[1], cmds[2], result);

  if (orig_log_level != msg_get_log_level())
    msg_info("Verbosity changed",
             evt_tag_int("log_level", msg_get_log_level()));

  if (success)
    g_string_printf(result, "OK syslog-ng log level set to %d", msg_get_log_level());
  else
    g_string_assign(result, "FAIL Invalid arguments received");
exit:
  g_strfreev(cmds);
  control_connection_send_reply(cc, result);
}

void
_wait_until_peer_disappears(ControlConnection *cc, gint max_seconds, gboolean *cancelled)
{
  while (max_seconds != 0 && !(*cancelled))
    {
      sleep(1);
      if (max_seconds > 0)
        max_seconds--;
      control_connection_send_batched_reply(cc, g_string_new("ALIVE\n"));
    }
  console_release();
}

typedef struct _AttachCommandArgs
{
  gboolean start_debugger;
  gint fds_to_steal;
  gint n_seconds;
  gboolean log_stderr;
  gint log_level;
} AttachCommandArgs;

static gboolean
_parse_attach_command_args(GString *command, AttachCommandArgs *args, GString *result)
{
  gboolean success = FALSE;
  gchar **cmds = g_strsplit(command->str, " ", 5);

  if (cmds[1] == NULL)
    {
      g_string_assign(result, "FAIL Invalid arguments received");
      goto exit;
    }

  if (g_str_equal(cmds[1], "STDIO"))
    {
      if (cmds[3])
        args->fds_to_steal = atoi(cmds[3]);
    }
  else if (g_str_equal(cmds[1], "LOGS"))
    {
      args->log_stderr = TRUE;
      /* NOTE: as log_stderr uses stderr (what a surprise)
       *          - we need to steal only the stderr
       *          - caller of the `attach logs` will get the stderr output as well, so
       *            they should redirect stderr to stdout if they want to capture it for
       *            further processing e.g.
       *                syslog-ng-ctl attach logs |& grep -i error
       */
      args->fds_to_steal = (1 << STDERR_FILENO);
    }
  else if (g_str_equal(cmds[1], "DEBUGGER"))
    {
      args->start_debugger = TRUE;
    }
  else
    {
      g_string_assign(result, "FAIL This version of syslog-ng only supports attaching to STDIO or LOGS");
      goto exit;
    }

  if (cmds[2])
    args->n_seconds = atoi(cmds[2]);

  if (cmds[4])
    args->log_level = msg_map_string_to_log_level(cmds[4]);
  if (args->log_level < 0)
    {
      g_string_assign(result, "FAIL Invalid log level");
      goto exit;
    }
  success = TRUE;

exit:
  g_strfreev(cmds);
  return success;
}

static void
control_connection_attach(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  MainLoop *main_loop = (MainLoop *) user_data;
  GString *result = g_string_sized_new(128);
  struct
  {
    gboolean log_stderr;
    gint log_level;
  } old_values =
  {
    log_stderr,
    msg_get_log_level()
  };
  AttachCommandArgs cmd_args =
  {
    .start_debugger = FALSE,
    .fds_to_steal = (1 << STDOUT_FILENO) | (1 << STDERR_FILENO),
    .n_seconds = -1,
    .log_stderr = old_values.log_stderr,
    .log_level = old_values.log_level
  };

  if (!_parse_attach_command_args(command, &cmd_args, result))
    goto exit;

  gint fds[3];
  gsize num_fds = G_N_ELEMENTS(fds);
  if (!control_connection_get_attached_fds(cc, fds, &num_fds) || num_fds != 3)
    {
      g_string_assign(result,
                      "FAIL The underlying transport for syslog-ng-ctl does not support fd passing or incorrect number of fds received");
      goto exit;
    }

  if (!console_acquire_from_fds(fds, cmd_args.fds_to_steal))
    {
      g_string_assign(result, "FAIL Error acquiring console");
      goto exit;
    }

  log_stderr = cmd_args.log_stderr;
  msg_set_log_level(cmd_args.log_level);

  if (cmd_args.start_debugger && !debugger_is_running())
    {
      //cfg_load_module(self->current_configuration, "mod-python");
      debugger_start(main_loop, main_loop_get_current_config(main_loop));
    }

  _wait_until_peer_disappears(cc, cmd_args.n_seconds, cancelled);

  if (cmd_args.start_debugger && debugger_is_running())
    debugger_stop();

  log_stderr = old_values.log_stderr;
  msg_set_log_level(old_values.log_level);

  g_string_assign(result, "OK [Console output ends here]");

exit:
  control_connection_send_batched_reply(cc, result);
  control_connection_send_close_batch(cc);
}

static void
control_connection_stop_process(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  GString *result = g_string_new("OK Shutdown initiated");
  MainLoop *main_loop = (MainLoop *) user_data;

  main_loop_exit(main_loop);

  control_connection_send_reply(cc, result);
}

static void
control_connection_config(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  MainLoop *main_loop = (MainLoop *) user_data;
  GlobalConfig *config = main_loop_get_current_config(main_loop);
  GString *result = g_string_sized_new(128);
  gchar **arguments = g_strsplit(command->str, " ", 0);

  if (!arguments[1])
    {
      g_string_assign(result, "FAIL Invalid arguments");
      goto exit;
    }

  if (g_str_equal(arguments[1], "ID"))
    {
      cfg_format_id(config, result);
      goto exit;
    }

  if (g_str_equal(arguments[1], "GET"))
    {
      if (g_str_equal(arguments[2], "ORIGINAL"))
        {
          g_string_assign(result, config->original_config->str);
          goto exit;
        }
      else if (g_str_equal(arguments[2], "PREPROCESSED"))
        {
          g_string_assign(result, config->preprocess_config->str);
          goto exit;
        }
    }

  if (g_str_equal(arguments[1], "VERIFY"))
    {
      main_loop_verify_config(result, main_loop);
      goto exit;
    }

  g_string_assign(result, "FAIL Invalid arguments received");

exit:
  g_strfreev(arguments);
  control_connection_send_reply(cc, result);
}

static void
show_ose_license_info(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  control_connection_send_reply(cc,
                                g_string_new("OK You are using the Open Source Edition of syslog-ng."));
}

static void
_respond_config_reload_status(gint type, gpointer user_data)
{
  gpointer *args = user_data;
  MainLoop *main_loop = (MainLoop *) args[0];
  ControlConnection *cc = (ControlConnection *) args[1];
  GString *reply;

  if (main_loop_was_last_reload_successful(main_loop))
    reply = g_string_new("OK Config reload successful");
  else
    reply = g_string_new("FAIL Config reload failed, reverted to previous config");

  control_connection_send_reply(cc, reply);
}

static void
control_connection_reload(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  MainLoop *main_loop = (MainLoop *) user_data;
  static gpointer args[2];
  GError *error = NULL;


  if (!main_loop_reload_config_prepare(main_loop, &error))
    {
      GString *result = g_string_new("");
      g_string_printf(result, "FAIL %s, previous config remained intact", error->message);
      g_clear_error(&error);
      control_connection_send_reply(cc, result);
      return;
    }

  args[0] = main_loop;
  args[1] = cc;
  register_application_hook(AH_CONFIG_CHANGED, _respond_config_reload_status, args, AHM_RUN_ONCE);
  main_loop_reload_config_commence(main_loop);
}

static void
control_connection_reopen(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  GString *result = g_string_new("OK Re-open of log destination files initiated");
  app_reopen_files();
  control_connection_send_reply(cc, result);
}

static const gchar *
secret_status_to_string(SecretStorageSecretState state)
{
  switch (state)
    {
    case SECRET_STORAGE_STATUS_PENDING:
      return "PENDING";
    case SECRET_STORAGE_SUCCESS:
      return "SUCCESS";
    case SECRET_STORAGE_STATUS_FAILED:
      return "FAILED";
    case SECRET_STORAGE_STATUS_INVALID_PASSWORD:
      return "INVALID_PASSWORD";
    default:
      g_assert_not_reached();
    }
  return "SHOULD NOT BE REACHED";
}

gboolean
secret_storage_status_accumulator(SecretStatus *status, gpointer user_data)
{
  GString *status_str = (GString *) user_data;
  g_string_append_printf(status_str, "%s\t%s\n", status->key, secret_status_to_string(status->state));
  return TRUE;
}

static GString *
process_credentials_status(GString *result)
{
  g_string_assign(result, "OK Credential storage status follows\n");
  secret_storage_status_foreach(secret_storage_status_accumulator, (gpointer) result);
  return result;
}

static GString *
process_credentials_add(GString *result, guint argc, gchar **arguments)
{
  if (argc < 4)
    {
      g_string_assign(result, "FAIL missing arguments to add\n");
      return result;
    }

  gchar *id = arguments[2];
  gchar *secret = arguments[3];

  if (secret_storage_store_secret(id, secret, strlen(secret)))
    g_string_assign(result, "OK Credentials stored successfully\n");
  else
    g_string_assign(result, "FAIL Error while saving credentials\n");

  secret_storage_wipe(secret, strlen(secret));
  return result;
}

static void
process_credentials(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  gchar **arguments = g_strsplit(command->str, " ", 4);
  guint argc = g_strv_length(arguments);

  GString *result = g_string_new(NULL);

  if (argc < 1)
    {
      g_string_assign(result, "FAIL missing subcommand\n");
      g_strfreev(arguments);
      control_connection_send_reply(cc, result);
      return;
    }

  gchar *subcommand = arguments[1];

  if (strcmp(subcommand, "status") == 0)
    result = process_credentials_status(result);
  else if (g_strcmp0(subcommand, "add") == 0)
    result = process_credentials_add(result, argc, arguments);
  else
    g_string_printf(result, "FAIL invalid subcommand %s\n", subcommand);

  g_strfreev(arguments);
  control_connection_send_reply(cc, result);
}

static void
control_connection_list_files(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  MainLoop *main_loop = (MainLoop *) user_data;
  GlobalConfig *config = main_loop_get_current_config(main_loop);
  GString *result = g_string_new("");

  for (GList *v = config->file_list; v; v = v->next)
    {
      CfgFilePath *cfg_file_path = (CfgFilePath *) v->data;
      g_string_append_printf(result, "%s: %s\n", cfg_file_path->path_type, cfg_file_path->file_path);
    }

  if (result->len == 0)
    g_string_assign(result, "No files available\n");

  control_connection_send_reply(cc, result);
}

static void
export_config_graph(ControlConnection *cc, GString *command, gpointer user_data, gboolean *cancelled)
{
  MainLoop *main_loop = (MainLoop *) user_data;
  GlobalConfig *cfg = main_loop_get_current_config(main_loop);
  GString *result = cfg_walker_generate_graph(cfg);

  control_connection_send_reply(cc, result);
}

ControlCommand default_commands[] =
{
  { "ATTACH", control_connection_attach, .threaded = TRUE },
  { "LOG", control_connection_message_log },
  { "STOP", control_connection_stop_process },
  { "RELOAD", control_connection_reload },
  { "REOPEN", control_connection_reopen },
  { "CONFIG", control_connection_config },
  { "LICENSE", show_ose_license_info },
  { "PWD", process_credentials },
  { "LISTFILES", control_connection_list_files },
  { "EXPORT_CONFIG_GRAPH", export_config_graph },
  { NULL, NULL },
};

void
main_loop_register_control_commands(MainLoop *main_loop)
{
  int i;
  ControlCommand *cmd;

  for (i = 0; default_commands[i].command_name != NULL; i++)
    {
      cmd = &default_commands[i];
      control_register_command(cmd->command_name, cmd->func, main_loop, cmd->threaded);
    }
}

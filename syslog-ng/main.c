/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#include "syslog-ng.h"
#include "cfg.h"
#include "messages.h"
#include "memtrace.h"
#include "children.h"
#include "stats/stats-registry.h"
#include "apphook.h"
#include "alarms.h"
#include "logqueue.h"
#include "gprocess.h"
#include "control/control.h"
#include "logsource.h"
#include "mainloop.h"
#include "plugin.h"
#include "reloc.h"
#include "console.h"
#include "resolved-configurable-paths.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <grp.h>

#if SYSLOG_NG_HAVE_GETOPT_H
#include <getopt.h>
#endif

#include <iv.h>
#include <iv_signal.h>

static gboolean display_version = FALSE;
static gboolean display_module_registry = FALSE;
static gboolean dummy = FALSE;

static MainLoopOptions main_loop_options;

#ifdef YYDEBUG
extern int cfg_parser_debug;
#endif

static GOptionEntry syslogng_options[] =
{
  { "version",           'V',         0, G_OPTION_ARG_NONE, &display_version, "Display version number (" SYSLOG_NG_PACKAGE_NAME " " SYSLOG_NG_COMBINED_VERSION ")", NULL },
  { "module-path",         0,         0, G_OPTION_ARG_STRING, &resolved_configurable_paths.initial_module_path, "Set the list of colon separated directories to search for modules, default=" SYSLOG_NG_MODULE_PATH, "<path>" },
  { "module-registry",     0,         0, G_OPTION_ARG_NONE, &display_module_registry, "Display module information", NULL },
  { "no-module-discovery", 0,         0, G_OPTION_ARG_NONE, &main_loop_options.disable_module_discovery, "Disable module auto-discovery, all modules need to be loaded explicitly by the configuration", NULL },
  { "seed",              'S',         0, G_OPTION_ARG_NONE, &dummy, "Does nothing, the need to seed the random generator is autodetected", NULL},
#ifdef YYDEBUG
  { "yydebug",           'y',         0, G_OPTION_ARG_NONE, &cfg_parser_debug, "Enable configuration parser debugging", NULL },
#endif
  { "cfgfile",           'f',         0, G_OPTION_ARG_STRING, &resolved_configurable_paths.cfgfilename, "Set config file name, default=" PATH_SYSLOG_NG_CONF, "<config>" },
  { "persist-file",      'R',         0, G_OPTION_ARG_STRING, &resolved_configurable_paths.persist_file, "Set the name of the persistent configuration file, default=" PATH_PERSIST_CONFIG, "<fname>" },
  { "preprocess-into",     0,         0, G_OPTION_ARG_STRING, &main_loop_options.preprocess_into, "Write the preprocessed configuration file to the file specified and quit", "output" },
  { "syntax-only",       's',         0, G_OPTION_ARG_NONE, &main_loop_options.syntax_only, "Only read and parse config file", NULL},
  { "check-startup",       0,         0, G_OPTION_ARG_NONE, &main_loop_options.check_startup, "Check if syslog-ng would start up and then exit", NULL},
  { "config-id",           0,         0, G_OPTION_ARG_NONE, &main_loop_options.config_id, "Parse config file, print configuration ID, and quit", NULL},
  { "control",           'c',         0, G_OPTION_ARG_STRING, &resolved_configurable_paths.ctlfilename, "Set syslog-ng control socket, default=" PATH_CONTROL_SOCKET, "<ctlpath>" },
  { "interactive",       'i',         0, G_OPTION_ARG_NONE, &main_loop_options.interactive_mode, "Enable interactive mode" },
  { NULL },
};


static void
resolve_paths_in_help_texts(void)
{
  for (GOptionEntry *oe = syslogng_options; oe->long_name; oe++)
    {
      oe->description = resolve_path_variables_in_text(oe->description);
    }
}

static void
free_help_texts(void)
{
  for (GOptionEntry *oe = syslogng_options; oe->long_name; oe++)
    {
      g_free((gpointer) oe->description);
    }
}


#define INSTALL_DAT_INSTALLER_VERSION "INSTALLER_VERSION"

static void
interactive_mode(void)
{
  debug_flag = FALSE;
  verbose_flag = FALSE;
  msg_init(TRUE);
  g_process_set_mode(G_PM_FOREGROUND);
}

gboolean
get_installer_version(gchar **inst_version)
{
  gchar line[1024];
  gboolean result = FALSE;
  FILE *f_install = fopen(get_installation_path_for(PATH_INSTALL_DAT), "r");

  if (!f_install)
    return FALSE;

  while (fgets(line, sizeof(line), f_install) != NULL)
    {
      if (strncmp(line, INSTALL_DAT_INSTALLER_VERSION, strlen(INSTALL_DAT_INSTALLER_VERSION)) == 0)
        {
          gchar *pos = strchr(line, '=');
          if (pos)
            {
              *inst_version = g_strdup(pos+1);
              result = TRUE;
              break;
            }
        }
    }
  fclose(f_install);
  return result;
}

#define ON_OFF_STR(x) (x ? "on" : "off")


void
version(void)
{
  gchar *installer_version;

  if (!get_installer_version(&installer_version) || installer_version == NULL)
    {
      installer_version = g_strdup(SYSLOG_NG_VERSION);
    }
  printf(SYSLOG_NG_PACKAGE_NAME " " SYSLOG_NG_COMBINED_VERSION "\n"
         "Config version: " VERSION_STR_LAST_SEMANTIC_CHANGE "\n"
         "Installer-Version: %s\n"
         "Revision: " SYSLOG_NG_SOURCE_REVISION "\n",
         installer_version);

#if SYSLOG_NG_WITH_COMPILE_DATE
  printf("Compile-Date: " __DATE__ " " __TIME__ "\n");
#endif

  printf("Module-Directory: %s\n", get_installation_path_for(SYSLOG_NG_PATH_MODULEDIR));
  printf("Module-Path: %s\n", resolved_configurable_paths.initial_module_path);
  printf("Include-Path: %s\n", get_installation_path_for(SYSLOG_NG_PATH_CONFIG_INCLUDEDIR));
  printf("Available-Modules: ");
  plugin_list_modules(stdout, FALSE);

  printf("Enable-Debug: %s\n"
         "Enable-GProf: %s\n"
         "Enable-Memtrace: %s\n"
         "Enable-Stackdump: %s\n"
         "Enable-IPv6: %s\n"
         "Enable-Spoof-Source: %s\n"
         "Enable-TCP-Wrapper: %s\n"
         "Enable-Linux-Caps: %s\n"
         "Enable-Systemd: %s\n",
         ON_OFF_STR(SYSLOG_NG_ENABLE_DEBUG),
         ON_OFF_STR(SYSLOG_NG_ENABLE_GPROF),
         ON_OFF_STR(SYSLOG_NG_ENABLE_MEMTRACE),
         ON_OFF_STR(SYSLOG_NG_ENABLE_STACKDUMP),
         ON_OFF_STR(SYSLOG_NG_ENABLE_IPV6),
         ON_OFF_STR(SYSLOG_NG_ENABLE_SPOOF_SOURCE),
         ON_OFF_STR(SYSLOG_NG_ENABLE_TCP_WRAPPER),
         ON_OFF_STR(SYSLOG_NG_ENABLE_LINUX_CAPS),
         ON_OFF_STR(SYSLOG_NG_ENABLE_SYSTEMD));

  g_free(installer_version);
}

#if SYSLOG_NG_ENABLE_LINUX_CAPS
#define BASE_CAPS "cap_net_bind_service,cap_net_broadcast,cap_net_raw," \
  "cap_dac_read_search,cap_dac_override,cap_chown,cap_fowner=p"

static void
setup_caps (void)
{
  static gchar caps[1024] = BASE_CAPS;

  if (geteuid() != 0)
    g_process_disable_caps();

  if (!g_process_is_cap_enabled())
    return;

  /* Set up the minimal privilege we'll need
   *
   * NOTE: polling /proc/kmsg requires cap_sys_admin, otherwise it'll always
   * indicate readability. Enabling/disabling cap_sys_admin on every poll
   * invocation seems to be too expensive. So I enable it for now.
   */

  /* NOTE: this only adds either cap_sys_admin or cap_bpf to the permitted
   * set, whereas the one below will add it to the effective and permitted
   * sets */

  if (g_process_check_cap_bpf())
    g_strlcat(caps, " cap_bpf=p", sizeof(caps));
  else
    g_strlcat(caps, " cap_sys_admin=p", sizeof(caps));

  if (g_process_check_cap_syslog())
    g_strlcat(caps, " cap_syslog=ep", sizeof(caps));
  else
    g_strlcat(caps, " cap_sys_admin=ep", sizeof(caps));

  g_process_set_caps(caps);
}
#else

#define setup_caps()

#endif

int
main(int argc, char *argv[])
{
  gint rc;
  GOptionContext *ctx;
  GError *error = NULL;

  console_global_init("syslog-ng");
  MainLoop *main_loop = main_loop_get_instance();

  z_mem_trace_init("syslog-ng.trace");

  g_process_set_name("syslog-ng");
  g_process_set_argv_space(argc, (gchar **) argv);

  resolved_configurable_paths_init(&resolved_configurable_paths);
  resolve_paths_in_help_texts();

  ctx = g_option_context_new(NULL);
  g_process_add_option_group(ctx);
  msg_add_option_group(ctx);
  g_option_context_add_main_entries(ctx, syslogng_options, NULL);
  main_loop_add_options(ctx);
  if (!g_option_context_parse(ctx, &argc, &argv, &error))
    {
      fprintf(stderr, "Error parsing command line arguments: %s\n", error ? error->message : "Invalid arguments");
      g_error_free(error);
      g_option_context_free(ctx);
      return 1;
    }
  g_option_context_free(ctx);
  free_help_texts();

  if (argc > 1)
    {
      fprintf(stderr, "Excess number of arguments\n");
      return 1;
    }

  if (display_version)
    {
      interactive_mode();
      version();
      return 0;
    }
  if (display_module_registry)
    {
      interactive_mode();
      plugin_list_modules(stdout, TRUE);
      return 0;
    }

  setup_caps();

  if (startup_debug_flag && debug_flag)
    {
      startup_debug_flag = FALSE;
    }

  if (startup_debug_flag)
    {
      debug_flag = TRUE;
    }

  if (debug_flag && !log_stderr)
    {
      fprintf(stderr,
              "The -d/--debug option no longer implies -e/--stderr, if you want to redirect internal() source to stderr please also include -e/--stderr option\n");
    }

  gboolean exit_before_main_loop_run = main_loop_options.syntax_only
                                       || main_loop_options.preprocess_into
                                       || main_loop_options.config_id
                                       || main_loop_options.check_startup;

  if (exit_before_main_loop_run)
    interactive_mode();

  if (debug_flag)
    {
      g_process_set_mode(G_PM_FOREGROUND);
    }

  /* in this case we switch users early while retaining a limited set of
   * credentials in order to initialize/reinitialize the configuration.
   */
  g_process_start();
  app_startup();

  main_loop_init(main_loop, &main_loop_options);
  rc = main_loop_read_and_init_config(main_loop);

  if (rc || exit_before_main_loop_run)
    {
      main_loop_deinit(main_loop);
      app_shutdown();
      reloc_deinit();
      g_process_startup_failed(rc, TRUE);
      return rc;
    }

  g_process_startup_ok();

  /* we are running as a non-root user from this point */

  app_post_daemonized();
  app_config_changed();

  if(startup_debug_flag)
    {
      debug_flag = FALSE;
      log_stderr = FALSE;
    }

  /* from now on internal messages are written to the system log as well */

  main_loop_run(main_loop);
  main_loop_deinit(main_loop);

  app_shutdown();
  z_mem_trace_dump();
  g_process_finish();
  console_global_deinit();
  reloc_deinit();
  return rc;
}

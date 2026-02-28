/*
 * Copyright (c) 2023 Balazs Scheidler <bazsi77@gmail.com>
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

#include "cfg-parser.h"
#include "plugin.h"
#include "plugin-types.h"
#include <bpf/libbpf.h>


extern CfgParser ebpf_parser;

static Plugin ebpf_plugins[] =
{
  {
    .type = LL_CONTEXT_INNER_SRC,
    .name = "ebpf",
    .parser = &ebpf_parser,
  },
};

int _libbpf_log(enum libbpf_print_level level, const char *fmt, va_list va) G_GNUC_PRINTF(2, 0);

int
_libbpf_log(enum libbpf_print_level level, const char *fmt, va_list va)
{
  int prio = EVT_PRI_INFO;
  switch (level)
    {
    case LIBBPF_DEBUG:
      prio = EVT_PRI_DEBUG;
      break;
    case LIBBPF_INFO:
      prio = EVT_PRI_INFO;
      break;
    case LIBBPF_WARN:
      prio = EVT_PRI_WARNING;
      break;
    default:
      prio = EVT_PRI_INFO;
      break;
    }

  if (prio == EVT_PRI_DEBUG && !debug_flag)
    return 0;

  gchar buf[2048];
  vsnprintf(buf, sizeof(buf), fmt, va);
  gsize len = strlen(buf);
  if (buf[len-1] == '\n')
    buf[len-1] = 0;
  msg_send_formatted_message(prio, buf);
  return 0;
}

libbpf_print_fn_t libbpf_set_print(libbpf_print_fn_t fn G_GNUC_PRINTF(2, 0));


gboolean
ebpf_module_init(PluginContext *context, CfgArgs *args)
{
  libbpf_set_print(_libbpf_log);

  plugin_register(context, ebpf_plugins, G_N_ELEMENTS(ebpf_plugins));
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "ebpf",
    .version = SYSLOG_NG_VERSION,
    .description = "EBPF Programs plugin",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = ebpf_plugins,
    .plugins_len = G_N_ELEMENTS(ebpf_plugins),
  };

  return &info;
}

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
#include "ebpf-reuseport.h"
#include "modules/afsocket/afsocket-signals.h"
#include "syslog-ng.h"
#include "messages.h"

#include <bpf/libbpf.h>

typedef struct _EBPFReusePort
{
  LogDriverPlugin super;
  struct random_kern *random;
  gint number_of_sockets;
} EBPFReusePort;

#include "random.skel.c"

int _libbpf_log(enum libbpf_print_level level, const char *fmt, va_list va) G_GNUC_PRINTF(2, 0);

void
ebpf_reuseport_set_sockets(LogDriverPlugin *s, gint number_of_sockets)
{
  EBPFReusePort *self = (EBPFReusePort *) s;
  self->number_of_sockets = number_of_sockets;
}

static void
_slot_setup_socket(EBPFReusePort *self, AFSocketSetupSocketSignalData *data)
{
  int bpf_fd = bpf_program__fd(self->random->progs.random_choice);
  if (bpf_fd < 0)
    {
      msg_error("ebpf-reuseport(): setsockopt(SO_ATTACH_REUSEPORT_EBPF) returned error",
                evt_tag_errno("error", errno));
      goto error;
    }

  if (setsockopt(data->sock, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, &bpf_fd, sizeof(bpf_fd)) < 0)
    {
      msg_error("ebpf-reuseport(): setsockopt(SO_ATTACH_REUSEPORT_EBPF) returned error",
                evt_tag_errno("error", errno));
      goto error;
    }

  msg_debug("ebpf-reuseport(): eBPF reuseport group randomizer applied",
            evt_tag_int("sock", data->sock));
  return;
error:
  data->failure = TRUE;
}

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

  msg_send_formatted_message(prio, buf);
  return 0;
}

libbpf_print_fn_t libbpf_set_print(libbpf_print_fn_t fn G_GNUC_PRINTF(2, 0));

static gboolean
_attach(LogDriverPlugin *s, LogDriver *driver)
{
  EBPFReusePort *self = (EBPFReusePort *)s;

  libbpf_set_print(_libbpf_log);

  self->random = random_kern__open_and_load();
  if (!self->random)
    {
      msg_error("ebpf-reuseport(): Unable to load eBPF program to the kernel");
      return FALSE;
    }
  self->random->bss->number_of_sockets = self->number_of_sockets;

  SignalSlotConnector *ssc = driver->signal_slot_connector;
  CONNECT(ssc, signal_afsocket_setup_socket, _slot_setup_socket, self);

  return TRUE;
}

static void
_detach(LogDriverPlugin *s, LogDriver *driver)
{
  EBPFReusePort *self = (EBPFReusePort *)s;

  SignalSlotConnector *ssc = driver->signal_slot_connector;
  DISCONNECT(ssc, signal_afsocket_setup_socket, _slot_setup_socket, self);
}

static void
_free(LogDriverPlugin *s)
{
  EBPFReusePort *self = (EBPFReusePort *) s;

  if (self->random)
    random_kern__destroy(self->random);
  log_driver_plugin_free_method(s);
}

LogDriverPlugin *
ebpf_reuseport_new(void)
{
  EBPFReusePort *self = g_new0(EBPFReusePort, 1);
  log_driver_plugin_init_instance(&self->super, "ebpf-reuseport");

  self->super.attach = _attach;
  self->super.detach = _detach;
  self->super.free_fn = _free;
  self->number_of_sockets = 0;

  return &self->super;
}

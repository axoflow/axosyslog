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

#include "control-client.h"
#include "gsocket.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

struct _ControlClient
{
  gchar *path;
  FILE *control_socket;
  gint control_fd;
};

ControlClient *
control_client_new(const gchar *name)
{
  ControlClient *self = g_new0(ControlClient, 1);

  self->path = g_strdup(name);

  return self;
}

gboolean
control_client_connect(ControlClient *self)
{
  GSockAddr *saddr = NULL;

  if (self->control_socket != NULL)
    {
      return TRUE;
    }

  saddr = g_sockaddr_unix_new(self->path);
  self->control_fd = socket(PF_UNIX, SOCK_STREAM, 0);

  if (self->control_fd == -1)
    {
      fprintf(stderr, "Error opening control socket, socket='%s', error='%s'\n", self->path, strerror(errno));
      goto error;
    }

  if (g_connect(self->control_fd, saddr) != G_IO_STATUS_NORMAL)
    {
      fprintf(stderr, "Error connecting control socket, socket='%s', error='%s'\n", self->path, strerror(errno));
      close(self->control_fd);
      goto error;
    }
  self->control_socket = fdopen(self->control_fd, "r+");
error:
  g_sockaddr_unref(saddr);
  return !!self->control_socket;
}

gint
control_client_send_command(ControlClient *self, const gchar *cmd, gboolean attach)
{
  struct iovec iov[1] =
  {
    { .iov_base = (gchar *) cmd, .iov_len = strlen(cmd) },
  };
  gint fds[3] = { 0, 1, 2 };
  union
  {
    char buf[CMSG_SPACE(sizeof(fds))];
    struct cmsghdr align;
  } u;
  struct msghdr msg =
  {
    .msg_iov = iov,
    .msg_iovlen = G_N_ELEMENTS(iov),
    0
  };
  if (attach)
    {
      msg.msg_control = u.buf;
      msg.msg_controllen = sizeof(u.buf);

      struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
      cmsg->cmsg_level = SOL_SOCKET;
      cmsg->cmsg_type = SCM_RIGHTS;
      cmsg->cmsg_len = CMSG_LEN(sizeof(fds));
      memcpy(CMSG_DATA(cmsg), fds, sizeof(fds));
    }

  return sendmsg(self->control_fd, &msg, 0);
//  return fwrite(cmd, strlen(cmd), 1, self->control_socket);
}

#define BUFF_LEN 8192

gint
control_client_read_reply(ControlClient *self, CommandResponseHandlerFunc response_handler, gpointer user_data)
{
  gint retval = 0;
  gchar buf[BUFF_LEN], *line;
  GString *chunk = g_string_sized_new(256);

  while (1)
    {
      line = fgets(buf, sizeof(buf), self->control_socket);

      if (!line)
        {
          fprintf(stderr, "Error reading or EOF occured on socket, error='%s'\n", strerror(errno));
          return 1;
        }

      if (strcmp(line, ".\n") == 0)
        break;

      g_string_assign(chunk, line);
      if (retval == 0)
        retval = response_handler(chunk, user_data);
    }

  g_string_free(chunk, TRUE);
  return retval;
}

void
control_client_free(ControlClient *self)
{
  if (self->control_socket)
    {
      fflush(self->control_socket);
      shutdown(self->control_fd, SHUT_RDWR);
      fclose(self->control_socket);
    }
  g_free(self->path);
  g_free(self);
}

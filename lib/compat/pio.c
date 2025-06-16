/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2011 Balázs Scheidler
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

#include "compat/pio.h"

#if !defined(SYSLOG_NG_HAVE_PREAD) || defined(SYSLOG_NG_HAVE_BROKEN_PREAD)

#include <sys/types.h>
#include <unistd.h>

ssize_t
bb__pread(int fd, void *buf, size_t count, off_t offset)
{
  ssize_t ret;
  off_t old_offset;

  old_offset = lseek(fd, 0, SEEK_CUR);
  if (old_offset == -1)
    return -1;

  if (lseek(fd, offset, SEEK_SET) < 0)
    return -1;

  ret = read(fd, buf, count);
  if (ret < 0)
    return -1;

  if (lseek(fd, old_offset, SEEK_SET) < 0)
    return -1;
  return ret;
}

ssize_t
bb__pwrite(int fd, const void *buf, size_t count, off_t offset)
{
  ssize_t ret;
  off_t old_offset;

  old_offset = lseek(fd, 0, SEEK_CUR);
  if (old_offset == -1)
    return -1;

  if (lseek(fd, offset, SEEK_SET) < 0)
    return -1;

  ret = write(fd, buf, count);
  if (ret < 0)
    return -1;

  if (lseek(fd, old_offset, SEEK_SET) < 0)
    return -1;
  return ret;
}
#endif

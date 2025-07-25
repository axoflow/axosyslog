/*
 * Copyright (c) 2002-2013 Balabit
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

#ifndef AFFILE_SOURCE_H_INCLUDED
#define AFFILE_SOURCE_H_INCLUDED

#include "driver.h"
#include "logreader.h"
#include "file-opener.h"
#include "file-reader.h"

typedef struct _AFFileSourceDriver
{
  LogSrcDriver super;
  GString *filename;
  FileReader *file_reader;
  FileOpener *file_opener;
  FileReaderOptions file_reader_options;
  FileOpenerOptions file_opener_options;
  gchar *transport_name;
  gsize transport_name_len;
} AFFileSourceDriver;

void affile_sd_set_transport_name(AFFileSourceDriver *s, const gchar *transport_name);
AFFileSourceDriver *affile_sd_new_instance(gchar *filename, GlobalConfig *cfg);
LogDriver *affile_sd_new(gchar *filename, GlobalConfig *cfg);


#endif

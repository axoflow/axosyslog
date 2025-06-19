/*
 * Copyright (c) 2002-2019 Balabit
 * Copyright (c) 2019 Mehul Prajapati
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

#include "cfg-path.h"

void
cfg_path_track_file(GlobalConfig *cfg, const gchar *file_path, const gchar *path_type)
{
  CfgFilePath *cfg_file_path = g_new0(CfgFilePath, 1);
  cfg_file_path->path_type = g_strdup(path_type);
  cfg_file_path->file_path = g_strdup(file_path);
  cfg->file_list = g_list_append(cfg->file_list, cfg_file_path);
}

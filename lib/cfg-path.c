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

static void
_track_path(GlobalConfig *cfg, const gchar *path, const gchar *path_type, CfgPathKind kind)
{
  CfgFilePath *cfg_file_path = g_new0(CfgFilePath, 1);
  cfg_file_path->path_type = g_strdup(path_type);
  cfg_file_path->file_path = g_strdup(path);
  cfg_file_path->kind = kind;
  cfg->file_list = g_list_append(cfg->file_list, cfg_file_path);
}

void
cfg_path_track_file(GlobalConfig *cfg, const gchar *file_path, const gchar *path_type)
{
  _track_path(cfg, file_path, path_type, CFG_PATH_FILE);
}

void
cfg_path_track_dir(GlobalConfig *cfg, const gchar *dir_path, const gchar *path_type)
{
  _track_path(cfg, dir_path, path_type, CFG_PATH_DIRECTORY);
}

GString *
cfg_path_format_file_list(GlobalConfig *cfg, gboolean include_directories)
{
  GString *result = g_string_new("");

  for (GList *v = cfg->file_list; v; v = v->next)
    {
      CfgFilePath *cfg_file_path = (CfgFilePath *) v->data;
      if (cfg_file_path->kind == CFG_PATH_DIRECTORY && !include_directories)
        continue;
      g_string_append_printf(result, "%s: %s\n", cfg_file_path->path_type, cfg_file_path->file_path);
    }

  if (result->len == 0)
    g_string_assign(result, "No files available\n");

  return result;
}

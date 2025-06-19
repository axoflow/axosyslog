/*
 * Copyright (c) 2015 Balabit
 * Copyright (c) 2015 Balázs Scheidler
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
#include "module-config.h"

gboolean
module_config_init(ModuleConfig *s, GlobalConfig *cfg)
{
  if (s->init)
    return s->init(s, cfg);
  return TRUE;
}

void
module_config_deinit(ModuleConfig *s, GlobalConfig *cfg)
{
  if (s->deinit)
    s->deinit(s, cfg);
}

void
module_config_free_method(ModuleConfig *s)
{
}

void
module_config_free(ModuleConfig *s)
{
  if (s->free_fn)
    s->free_fn(s);
  g_free(s);
}

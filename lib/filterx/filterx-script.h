/*
 * Copyright (c) 2026 Axoflow
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

#ifndef FILTERX_SCRIPT_H_INCLUDED
#define FILTERX_SCRIPT_H_INCLUDED

#include "cfg.h"

/* For filterx-tool */

typedef struct _FilterXScript FilterXScript;

FilterXScript *filterx_script_new(GlobalConfig *cfg);
gboolean filterx_script_load_file(FilterXScript *self, const gchar *fname);
gboolean filterx_script_load_buffer(FilterXScript *self, const gchar *buffer, gsize length);
gboolean filterx_script_init(FilterXScript *self);
gboolean filterx_script_deinit(FilterXScript *self);
void filterx_script_format_ast_json(FilterXScript *self, GString *json);
void filterx_script_free(FilterXScript *self);

#endif

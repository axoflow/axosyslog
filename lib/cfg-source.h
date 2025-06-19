/*
 * Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#ifndef CFG_SOURCE_H_INCLUDED
#define CFG_SOURCE_H_INCLUDED

#include "cfg-lexer.h"

gboolean cfg_source_print_source_text(const gchar *filename, gint line, gint column, gint offset);

/* These functions are only available during parsing */
gboolean cfg_source_print_source_context(CfgLexer *lexer, CfgIncludeLevel *level, const CFG_LTYPE *yylloc);
gboolean cfg_source_extract_source_text(CfgLexer *lexer, const CFG_LTYPE *yylloc, GString *result);

#endif

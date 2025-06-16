/*
 * Copyright (c) 2010-2013 Balabit
 * Copyright (c) 2010-2013 Balázs Scheidler
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

#ifndef CFG_LEXER_SUBST_H_INCLUDED
#define CFG_LEXER_SUBST_H_INCLUDED 1

#include "syslog-ng.h"
#include "cfg-args.h"

typedef struct _CfgLexerSubst CfgLexerSubst;

gchar *cfg_lexer_subst_invoke(CfgLexerSubst *self, const gchar *input, gssize input_len, gsize *output_length,
                              GError **error);

CfgLexerSubst *cfg_lexer_subst_new(CfgArgs *globals, CfgArgs *defs, CfgArgs *args);
void cfg_lexer_subst_free(CfgLexerSubst *self);

gchar *
cfg_lexer_subst_args_in_input(CfgArgs *globals, CfgArgs *defs, CfgArgs *args, const gchar *input, gssize input_length,
                              gsize *output_length, GError **error);

#endif

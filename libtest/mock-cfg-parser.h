/*
 * Copyright (c) 2018 Balabit
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
#ifndef LIBTEST_MOCK_CFG_PARSER_H_INCLUDED
#define LIBTEST_MOCK_CFG_PARSER_H_INCLUDED

#include "cfg-lexer.h"

typedef struct
{
  CFG_STYPE *yylval;
  CFG_LTYPE *yylloc;
  CfgLexer *lexer;
} CfgParserMock;

void cfg_parser_mock_input(CfgParserMock *self, const gchar *buffer);
void cfg_parser_mock_clear_token(CfgParserMock *self);
void cfg_parser_mock_next_token(CfgParserMock *self);


CfgParserMock *cfg_parser_mock_new(void);
void cfg_parser_mock_free(CfgParserMock *self);

#endif

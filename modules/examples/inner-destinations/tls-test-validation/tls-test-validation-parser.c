/*
 * Copyright (c) 2023 Ricardo Filipe <ricardo.l.filipe@tecnico.ulisboa.pt>
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

#include "tls-test-validation.h"
#include "cfg-parser.h"
#include "tls-test-validation-grammar.h"

extern int tls_test_validation_debug;
int tls_test_validation_parse(CfgLexer *lexer, LogDriverPlugin **instance, gpointer arg);

static CfgLexerKeyword tls_test_validation_keywords[] =
{
  { "tls_test_validation",  KW_TLS_TEST_VALIDATION },
  { "identity", KW_IDENTITY },
  { NULL }
};

CfgParser tls_test_validation_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &tls_test_validation_debug,
#endif
  .name = "tls_test_validation",
  .keywords = tls_test_validation_keywords,
  .parse = (int (*)(CfgLexer *lexer, gpointer *instance, gpointer arg)) tls_test_validation_parse,
  .cleanup = (void (*)(gpointer)) log_driver_plugin_free
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(tls_test_validation_, TLS_TEST_VALIDATION_, LogDriverPlugin **)

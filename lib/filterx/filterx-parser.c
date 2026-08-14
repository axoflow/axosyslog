/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/filterx-parser.h"
#include "filterx/filterx-grammar.h"
#include "filterx/filterx-ast.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-pipe.h"
#include "cfg.h"
#include "cfg-lexer.h"
#include "messages.h"

extern int filterx_debug;
int filterx_parse(CfgLexer *lexer, FilterXExpr **expr, gpointer arg);

static CfgLexerKeyword filterx_keywords[] =
{
  { "or",                 KW_OR },
  { "and",                KW_AND },
  { "in",                 KW_IN },
  { "not",                KW_NOT },
  { "lt",                 KW_STR_LT },
  { "le",                 KW_STR_LE },
  { "eq",                 KW_STR_EQ },
  { "ne",                 KW_STR_NE },
  { "ge",                 KW_STR_GE },
  { "gt",                 KW_STR_GT },

  { "true",               KW_TRUE },
  { "false",              KW_FALSE },
  { "null",               KW_NULL },
  { "enum",               KW_ENUM },

  { "if",                 KW_IF },
  { "else",               KW_ELSE },
  { "elif",               KW_ELIF },
  { "switch",             KW_SWITCH },
  { "case",               KW_CASE },
  { "default",            KW_DEFAULT },

  { "isset",              KW_ISSET },
  { "declare",            KW_DECLARE },
  { "drop",               KW_DROP },
  { "done",               KW_DONE },
  { "break",              KW_BREAK },
  { "dpath",              KW_DPATH },

  { CFG_KEYWORD_STOP },
};

CfgParser filterx_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &filterx_debug,
#endif
  .name = "filterx expression",
  .context = LL_CONTEXT_FILTERX,
  .keywords = filterx_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) filterx_parse,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(filterx_, FILTERX_, FilterXExpr **)

/* for parsing the whole as a brace-less filterx statement list */
static void
_inject_toplevel_token(CfgLexer *lexer)
{
  CfgTokenBlock *block = cfg_token_block_new();
  CFG_STYPE token;

  memset(&token, 0, sizeof(token));
  token.type = LL_TOKEN;
  token.token = LL_FILTERX_VIRTUAL_TOPLEVEL;
  cfg_token_block_add_and_consume_token(block, &token);

  cfg_lexer_inject_token_block(lexer, block);
}

static gboolean
_parse_script(GlobalConfig *cfg, CfgLexer *lexer, FilterXExpr **block)
{
  FilterXEvalContext compile_context;

  _inject_toplevel_token(lexer);

  filterx_eval_begin_compile(&compile_context, cfg);
  gboolean result = cfg_run_parser(cfg, lexer, &filterx_parser, (gpointer *) block, NULL);
  filterx_eval_end_compile(&compile_context);

  return result;
}

/* To obtain errors beyond compilability. */
static gboolean
_compile_block(GlobalConfig *cfg, FilterXExpr *block, GString **ast)
{
  /* the pipe takes over @block and optimizes it as part of its initialization */
  LogPipe *pipe = log_filterx_pipe_new(block, cfg);

  gboolean result = log_pipe_init(pipe);
  if (result)
    {
      /* dump the tree that would actually be evaluated, e.g. the optimized one */
      if (ast)
        *ast = filterx_expr_format_ast_string(log_filterx_pipe_get_block(pipe));
      log_pipe_deinit(pipe);
    }

  log_pipe_unref(pipe);
  return result;
}

gboolean
filterx_compile_script(GlobalConfig *cfg, CfgLexer *lexer, GString **ast)
{
  FilterXExpr *block = NULL;

  if (!_parse_script(cfg, lexer, &block))
    return FALSE;

  return _compile_block(cfg, block, ast);
}

gboolean
filterx_compile_script_file(GlobalConfig *cfg, const gchar *fname, GString **ast)
{
  FILE *script_file;

  cfg_discover_candidate_modules(cfg);

  cfg->filename = fname;

  if ((script_file = fopen(fname, "r")) == NULL)
    {
      msg_error("Error opening filterx script file",
                evt_tag_str(EVT_TAG_FILENAME, fname),
                evt_tag_error(EVT_TAG_OSERROR));
      return FALSE;
    }

  gboolean result = filterx_compile_script(cfg, cfg_lexer_new(cfg, script_file, fname, NULL), ast);
  fclose(script_file);

  return result;
}

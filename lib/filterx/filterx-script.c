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

#include "filterx/filterx-script.h"
#include "filterx/filterx-parser.h"
#include "filterx/filterx-grammar.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-config.h"
#include "filterx/filterx-type-inference.h"
#include "filterx/filterx-scope-var-layout.h"
#include "filterx/filterx-ast.h"
#include "filterx/jit/jit.h"
#include "cfg-lexer.h"
#include "messages.h"

struct _FilterXScript
{
  GlobalConfig *cfg;
  FilterXExpr *block;
  FilterXScopeVariableLayout *scope_var_layout;
};

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
_load(FilterXScript *self, CfgLexer *lexer)
{
  FilterXEvalContext compile_context;

  _inject_toplevel_token(lexer);

  filterx_eval_begin_compile(&compile_context, self->cfg);
  gboolean success = cfg_run_parser(self->cfg, lexer, &filterx_parser, (gpointer *) &self->block, NULL);
  filterx_eval_end_compile(&compile_context);

  if (!success)
    {
      filterx_expr_unref(self->block);
      self->block = NULL;
    }

  return success;
}

gboolean
filterx_script_load_buffer(FilterXScript *self, const gchar *buffer, gsize length)
{
  return _load(self, cfg_lexer_new_buffer(self->cfg, buffer, length));
}

gboolean
filterx_script_load_file(FilterXScript *self, const gchar *fname)
{
  cfg_discover_candidate_modules(self->cfg);
  self->cfg->filename = fname;

  FILE *file = fopen(fname, "r");
  if (!file)
    {
      msg_error("Error opening filterx script file",
                evt_tag_str(EVT_TAG_FILENAME, fname),
                evt_tag_error(EVT_TAG_OSERROR));
      return FALSE;
    }

  gboolean success = _load(self, cfg_lexer_new(self->cfg, file, fname, NULL));
  fclose(file);

  return success;
}

static void
_compile_block(FilterXScript *self)
{
  FilterXJIT *jit = filterx_config_get(self->cfg)->jit;

  if (!jit)
    return;

  if (!filterx_expr_can_compile(self->block))
    return;

  filterx_jit_ir_add_new_block(jit, "script", self->scope_var_layout);
  FilterXIRValue result = filterx_expr_compile(self->block, jit);
  filterx_jit_ir_finish_current_block(jit, result);
}

static void
_infer_block_types(FilterXScript *self)
{
#if SYSLOG_NG_ENABLE_JIT
  FilterXTypeEnv *env = filterx_type_env_new();
  filterx_expr_infer_types(self->block, env);
  filterx_type_env_trace_dump(env, "end of script");
  filterx_type_env_free(env);
#endif
}

static gboolean
_initialize_block(FilterXScript *self)
{
  self->block = filterx_expr_optimize(self->block);

  if (!filterx_expr_init(self->block, self->cfg))
    return FALSE;

  _infer_block_types(self);
  self->scope_var_layout = filterx_scope_variable_layout_new(self->block);
  _compile_block(self);
  return TRUE;
}

gboolean
filterx_script_init(FilterXScript *self)
{
  FilterXEvalContext compile_context;

  filterx_eval_begin_compile(&compile_context, self->cfg);
  gboolean success = _initialize_block(self);
  if (!success)
    filterx_eval_dump_errors("FILTERX ERROR");
  filterx_eval_end_compile(&compile_context);

  return success;
}

gboolean
filterx_script_deinit(FilterXScript *self)
{
  filterx_scope_variable_layout_free(self->scope_var_layout);
  self->scope_var_layout = NULL;

  filterx_expr_deinit(self->block, self->cfg);
  return TRUE;
}

void
filterx_script_format_ast_json(FilterXScript *self, GString *json)
{
  struct json_object *root = filterx_expr_format_ast(self->block);

  g_string_append(json, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
  json_object_put(root);
}

FilterXScript *
filterx_script_new(GlobalConfig *cfg)
{
  FilterXScript *self = g_new0(FilterXScript, 1);

  self->cfg = cfg;
  return self;
}

void
filterx_script_free(FilterXScript *self)
{
  filterx_expr_unref(self->block);
  g_free(self);
}

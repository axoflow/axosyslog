/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

/* A fresh GlobalConfig is used per input: parsing declare/enum statements mutates
 * config-level registries, so reusing one config would let state leak across
 * inputs and manufacture false-positive crashes.
 */

#include <stddef.h>
#include <stdint.h>
#include <glib.h>

#include "apphook.h"
#include "cfg.h"
#include "cfg-lexer.h"
#include "scratch-buffers.h"
#include "logmsg/logmsg.h"
#include "filterx/filterx-parser.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-scope.h"
#include "filterx/filterx-scope-var-layout.h"
#include "filterx/filterx-object.h"

/* Bison error recovery discards semantic values without freeing them, so every
 * malformed input leaks and LeakSanitizer, which libFuzzer enables by default,
 * aborts before the target reaches its second input. Suppressing by the
 * yyparse frame keeps leak detection alive for optimize, init and eval. */
const char *
__lsan_default_suppressions(void)
{
  return "leak:filterx_parse";
}

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  app_startup();
  return 0;
}

static FilterXExpr *
_compile(GlobalConfig *cfg, const uint8_t *data, size_t size)
{
  /* cfg_run_parser() takes ownership of the lexer (frees it) and, via
   * cfg_parser_parse(), pushes the parser's own LL_CONTEXT_FILTERX context.
   * Wrap it in a compile context so grammar-time constant folding has one, matching
   * the filterx_content rule in cfg-grammar.y. */
  CfgLexer *lexer = cfg_lexer_new_buffer(cfg, (const gchar *) data, size);
  FilterXExpr *block = NULL;

  FilterXEvalContext compile_context;
  filterx_eval_begin_compile(&compile_context, cfg);
  gboolean ok = cfg_run_parser(cfg, lexer, &filterx_parser, (gpointer *) &block, NULL);
  filterx_eval_end_compile(&compile_context);

  if (!ok)
    {
      filterx_expr_unref(block);
      return NULL;
    }
  return block;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  GlobalConfig *cfg = cfg_new_snippet();

  FilterXExpr *block = _compile(cfg, data, size);
  if (block)
    {
      /* init pushes its errors into the active context, like optimize */
      FilterXEvalContext compile_context;
      filterx_eval_begin_compile(&compile_context, cfg);
      block = filterx_expr_optimize(block);
      gboolean inited = block && filterx_expr_init(block, cfg);
      filterx_eval_end_compile(&compile_context);

      if (inited)
        {
          /* The compiled block addresses its variables by index into a scope
           * laid out from the block itself (what log_filterx_pipe does); a scope
           * without that layout would assert on the first variable access. */
          FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new(block);
          LogMessage *msg = log_msg_new_empty();
          FilterXScope *scope = filterx_scope_new(NULL, layout);
          FilterXEvalContext context;
          filterx_eval_begin_context(&context, NULL, scope, msg);

          FilterXObject *result = filterx_expr_eval(block);
          filterx_object_unref(result);

          filterx_scope_free(scope);
          filterx_eval_clear_errors();
          filterx_eval_end_context(&context);
          log_msg_unref(msg);
          filterx_scope_variable_layout_free(layout);

          filterx_expr_deinit(block, cfg);
        }
      filterx_expr_unref(block);
    }

  cfg_free(cfg);

  scratch_buffers_explicit_gc();   /* keep the per-thread scratch pool bounded across iterations */
  return 0;
}

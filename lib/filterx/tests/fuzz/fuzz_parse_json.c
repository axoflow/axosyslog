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

#include <stddef.h>
#include <stdint.h>
#include <glib.h>

#include "apphook.h"
#include "scratch-buffers.h"
#include "logmsg/logmsg.h"
#include "filterx/json-repr.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-scope.h"

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  app_startup();
  return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  /* filterx_object_from_json() stores weak refs in the active eval context, so
   * establish (and tear down) a fresh one per input for isolation. */
  LogMessage *msg = log_msg_new_empty();
  FilterXScope *scope = filterx_scope_new(NULL, NULL);
  FilterXEvalContext context;
  filterx_eval_begin_context(&context, NULL, scope, msg);

  GError *error = NULL;
  FilterXObject *obj = filterx_object_from_json((const gchar *) data, size, &error);
  filterx_object_unref(obj);
  if (error)
    g_error_free(error);

  filterx_scope_free(scope);
  filterx_eval_clear_errors();
  filterx_eval_end_context(&context);
  log_msg_unref(msg);

  scratch_buffers_explicit_gc();   /* keep the per-thread scratch pool bounded across iterations */
  return 0;
}

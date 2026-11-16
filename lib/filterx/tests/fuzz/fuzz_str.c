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
#include <string.h>
#include <glib.h>

#include "apphook.h"
#include "scratch-buffers.h"
#include "logmsg/logmsg.h"
#include "filterx/func-str.h"
#include "filterx/func-str-transform.h"
#include "filterx/object-string.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-scope.h"

typedef FilterXObject *(*SimpleFunc)(FilterXExpr *s, FilterXObject *args[], gsize args_len);

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  app_startup();
  return 0;
}

static void
_run1(SimpleFunc fn, FilterXObject *arg)
{
  FilterXObject *args[] = { arg };
  FilterXObject *result = fn(NULL, args, 1);
  filterx_object_unref(result);
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  LogMessage *msg = log_msg_new_empty();
  FilterXScope *scope = filterx_scope_new(NULL, NULL);
  FilterXEvalContext context;
  filterx_eval_begin_context(&context, NULL, scope, msg);

  FilterXObject *whole = filterx_string_new((const gchar *) data, (gssize) size);
  _run1(filterx_simple_function_utf8_validate, whole);
  _run1(filterx_simple_function_utf8_sanitize, whole);
  _run1(filterx_simple_function_lower, whole);
  _run1(filterx_simple_function_upper, whole);
  _run1(filterx_simple_function_str_strip, whole);
  _run1(filterx_simple_function_str_lstrip, whole);
  _run1(filterx_simple_function_str_rstrip, whole);

  const uint8_t *nl1 = memchr(data, '\n', size);
  if (nl1)
    {
      gsize hay_len = (gsize) (nl1 - data);
      const uint8_t *rest = nl1 + 1;
      gsize rest_len = size - hay_len - 1;
      const uint8_t *nl2 = memchr(rest, '\n', rest_len);
      gsize old_len = nl2 ? (gsize) (nl2 - rest) : rest_len;
      const uint8_t *new_p = nl2 ? nl2 + 1 : rest + rest_len;
      gsize new_len = nl2 ? rest_len - old_len - 1 : 0;

      FilterXObject *hay = filterx_string_new((const gchar *) data, (gssize) hay_len);
      FilterXObject *old = filterx_string_new((const gchar *) rest, (gssize) old_len);
      FilterXObject *rep = filterx_string_new((const gchar *) new_p, (gssize) new_len);
      FilterXObject *args3[] = { hay, old, rep };
      FilterXObject *result = filterx_simple_function_str_replace(NULL, args3, 3);
      filterx_object_unref(result);
      filterx_object_unref(hay);
      filterx_object_unref(old);
      filterx_object_unref(rep);
    }

  filterx_object_unref(whole);

  filterx_scope_free(scope);
  filterx_eval_clear_errors();
  filterx_eval_end_context(&context);
  log_msg_unref(msg);

  scratch_buffers_explicit_gc();   /* keep the per-thread scratch pool bounded across iterations */
  return 0;
}

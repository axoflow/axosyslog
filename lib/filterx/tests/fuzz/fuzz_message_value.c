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

/* The first input byte indexes value_types[], the rest is the stored
 * representation. The committed corpus depends on the order of the table. */

#include <stddef.h>
#include <stdint.h>
#include <glib.h>

#include "apphook.h"
#include "scratch-buffers.h"
#include "logmsg/logmsg.h"
#include "filterx/object-message-value.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-scope.h"

/* The tag values a real NV pair can carry. __COMPAT_LM_VT_INT32, which
 * deserialization rewrites to LM_VT_INTEGER, and the LM_VT_NONE sentinel are
 * left out. */
static const LogMessageValueType value_types[] =
{
  LM_VT_STRING, LM_VT_JSON, LM_VT_BOOLEAN, LM_VT_INTEGER, LM_VT_DOUBLE,
  LM_VT_DATETIME, LM_VT_LIST, LM_VT_NULL, LM_VT_BYTES, LM_VT_PROTOBUF,
};

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
  app_startup();
  return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size < 1)
    return 0;

  LogMessageValueType type = value_types[data[0] % G_N_ELEMENTS(value_types)];
  const gchar *repr = (const gchar *) (data + 1);
  gssize repr_len = (gssize) (size - 1);

  /* Message-value objects stash weak refs in the active eval context, so
   * establish (and tear down) a fresh one per input. */
  LogMessage *msg = log_msg_new_empty();
  FilterXScope *scope = filterx_scope_new(NULL, NULL);
  FilterXEvalContext context;
  filterx_eval_begin_context(&context, NULL, scope, msg);

  FilterXObject *mv = filterx_message_value_new(repr, repr_len, type);
  if (mv)
    {
      FilterXObject *unmarshalled = filterx_object_unmarshal(mv);
      if (unmarshalled)
        {
          GString *remarshalled = scratch_buffers_alloc();
          LogMessageValueType out_type;
          filterx_object_marshal(unmarshalled, remarshalled, &out_type);
          filterx_object_unref(unmarshalled);
        }
      filterx_object_unref(mv);
    }

  filterx_scope_free(scope);
  filterx_eval_clear_errors();
  filterx_eval_end_context(&context);
  log_msg_unref(msg);

  scratch_buffers_explicit_gc();   /* keep the per-thread scratch pool bounded across iterations */
  return 0;
}

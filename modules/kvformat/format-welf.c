/*
 * Copyright (c) 2015 Balabit
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

#include "format-welf.h"
#include "utf8utils.h"
#include "value-pairs/cmdline.h"

typedef struct _TFWelfState
{
  TFSimpleFuncState super;
  ValuePairs *vp;
} TFWelfState;

typedef struct _TFWelfIterState
{
  GString *result;
  gboolean initial_kv_pair_printed;
} TFWelfIterState;

static gboolean
tf_format_welf_prepare(LogTemplateFunction *self, gpointer s, LogTemplate *parent,
                       gint argc, gchar *argv[],
                       GError **error)
{
  TFWelfState *state = (TFWelfState *) s;

  state->vp = value_pairs_new_from_cmdline (parent->cfg, &argc, &argv, NULL, NULL, error);
  if (!state->vp)
    return FALSE;

  return TRUE;
}

static gboolean
tf_format_welf_foreach(const gchar *name, LogMessageValueType type, const gchar *value,
                       gsize value_len, gpointer user_data)
{
  TFWelfIterState *iter_state = (TFWelfIterState *) user_data;
  GString *result = iter_state->result;

  if (iter_state->initial_kv_pair_printed)
    g_string_append(result, " ");
  else
    iter_state->initial_kv_pair_printed = TRUE;

  g_string_append(result, name);
  g_string_append_c(result, '=');
  if (memchr(value, ' ', value_len) == NULL)
    append_unsafe_utf8_as_escaped_binary(result, value, value_len, 0);
  else
    {
      g_string_append_c(result, '"');
      append_unsafe_utf8_as_escaped_binary(result, value, value_len, AUTF8_UNSAFE_QUOTE);
      g_string_append_c(result, '"');
    }

  return FALSE;
}

static gint
tf_format_welf_strcmp(gconstpointer a, gconstpointer b)
{
  gchar *sa = (gchar *)a, *sb = (gchar *)b;
  if (strcmp (sa, "id") == 0)
    return -1;
  return strcmp(sa, sb);
}

static void
tf_format_welf_call(LogTemplateFunction *self, gpointer s, const LogTemplateInvokeArgs *args, GString *result,
                    LogMessageValueType *type)
{
  TFWelfState *state = (TFWelfState *) s;
  TFWelfIterState iter_state =
  {
    .result = result,
    .initial_kv_pair_printed = FALSE
  };
  gint i;

  *type = LM_VT_STRING;
  for (i = 0; i < args->num_messages; i++)
    {
      value_pairs_foreach_sorted(state->vp,
                                 tf_format_welf_foreach, (GCompareFunc) tf_format_welf_strcmp,
                                 args->messages[i], args->options, &iter_state);
    }

}

static void
tf_format_welf_free_state(gpointer s)
{
  TFWelfState *state = (TFWelfState *) s;

  value_pairs_unref(state->vp);
  tf_simple_func_free_state(s);
}

TEMPLATE_FUNCTION(TFWelfState, tf_format_welf, tf_format_welf_prepare, NULL, tf_format_welf_call,
                  tf_format_welf_free_state, NULL);

/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 László Várady
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

#include <criterion/criterion.h>

#include "syslog-ng.h"
#include "scratch-buffers.h"
#include "libtest/filterx-lib.h"
#include "apphook.h"
#include "logmsg/logmsg.h"
#include "logpipe.h"

#include "filterx/filterx-scope.h"
#include "filterx/filterx-variable.h"
#include "filterx/object-primitive.h"

Test(filterx_scope, test_scope_on_heap)
{
  FilterXVariableHandle handles[] = {filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING)};
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *s = filterx_scope_new(NULL, l);
  cr_assert_null(filterx_scope_lookup_variable(s, filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING), 0));
  filterx_scope_free(s);
  filterx_scope_variable_layout_free(l);
}

Test(filterx_scope, test_scope_stacking)
{
  FilterXVariableHandle handles[] = {filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING)};
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *s = filterx_scope_new(NULL, l);

  FilterXVariableHandle var = filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING);
  filterx_scope_register_variable(s, FX_VAR_DECLARED_FLOATING, var, 0);

  FilterXScope *s2 = filterx_scope_new(s, l);

  cr_assert_not_null(filterx_scope_lookup_variable(s2, var, 0));

  filterx_scope_free(s2);
  filterx_scope_free(s);
  filterx_scope_variable_layout_free(l);
}

Test(filterx_scope, test_scope_sync)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;

  FilterXVariableHandle handles[] = {filterx_map_varname_to_handle("$var", FX_VAR_MESSAGE_TIED)};
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *s = filterx_scope_new(NULL, l);
  LogMessage *msg = log_msg_new_empty();

  filterx_scope_set_message(s, msg);

  FilterXVariableHandle var = filterx_map_varname_to_handle("$var", FX_VAR_MESSAGE_TIED);
  FilterXVariable *v = filterx_scope_register_variable(s, FX_VAR_MESSAGE_TIED, var, 0);
  FilterXObject *o = filterx_boolean_new(TRUE);
  filterx_scope_set_variable(s, v, &o, TRUE);
  filterx_object_unref(o);

  filterx_scope_set_dirty(s);
  filterx_scope_sync(s, &msg, &path_options);

  LogMessageValueType type;
  const gchar *value = log_msg_get_value_with_type(msg, filterx_variable_get_nv_handle(v), NULL, &type);
  cr_assert_eq(type, LM_VT_BOOLEAN);
  cr_assert_str_eq(value, "true");

  cr_assert(filterx_scope_lookup_variable(s, var, 0));
  log_msg_set_value_by_name(msg, "var", "newvalue", 0);
  cr_assert_not(filterx_scope_lookup_variable(s, var, 0));

  log_msg_unref(msg);
  filterx_scope_free(s);
  filterx_scope_variable_layout_free(l);
}

Test(filterx_scope, test_scope_sync_walks_parent_scopes_until_fork_point)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;

  FilterXVariableHandle handles[] = {filterx_map_varname_to_handle("$var", FX_VAR_MESSAGE_TIED)};
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *s = filterx_scope_new(NULL, l);
  LogMessage *msg = log_msg_new_empty();
  filterx_scope_set_message(s, msg);

  FilterXVariableHandle var = filterx_map_varname_to_handle("$var", FX_VAR_MESSAGE_TIED);
  FilterXVariable *v = filterx_scope_register_variable(s, FX_VAR_MESSAGE_TIED, var, 0);
  FilterXObject *o = filterx_boolean_new(TRUE);
  filterx_scope_set_variable(s, v, &o, TRUE);
  filterx_object_unref(o);
  filterx_scope_set_dirty(s);

  FilterXScope *s2 = filterx_scope_new(s, l);
  filterx_scope_sync(s2, &msg, &path_options);

  LogMessageValueType type;
  const gchar *value = log_msg_get_value_with_type(msg, filterx_variable_get_nv_handle(v), NULL, &type);
  cr_assert_eq(type, LM_VT_BOOLEAN);
  cr_assert_str_eq(value, "true");

  log_msg_unref(msg);
  filterx_scope_free(s2);
  filterx_scope_free(s);
  filterx_scope_variable_layout_free(l);
}

static gboolean
_assert_var_false(FilterXVariable *variable, gpointer user_data)
{
  cr_assert_not(filterx_boolean_get_value(variable->value));
  return TRUE;
}

Test(filterx_scope, test_scope_foreach_variable_with_parent_scope)
{
  FilterXVariableHandle handles[] = {filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING)};
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *s = filterx_scope_new(NULL, l);

  FilterXVariableHandle var = filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING);
  FilterXVariable *v = filterx_scope_register_variable(s, FX_VAR_DECLARED_FLOATING, var, 0);
  FilterXObject *o = filterx_boolean_new(TRUE);
  filterx_scope_set_variable(s, v, &o, TRUE);
  filterx_object_unref(o);

  FilterXScope *s2 = filterx_scope_new(s, l);

  v = filterx_scope_register_variable(s2, FX_VAR_DECLARED_FLOATING, var, 0);
  o = filterx_boolean_new(FALSE);
  filterx_scope_set_variable(s, v, &o, TRUE);
  filterx_object_unref(o);

  cr_assert(filterx_scope_foreach_variable_readonly(s2, _assert_var_false, NULL));

  filterx_scope_free(s2);
  filterx_scope_free(s);
  filterx_scope_variable_layout_free(l);
}

static FilterXObject *
_retain_ref(FilterXObject *value, gpointer user_data)
{
  return filterx_object_ref(value);
}

typedef struct _DupCheckState
{
  FilterXVariableHandle msg_tied_handle;
  FilterXVariableHandle floating_handle;
  gint num_variables_seen;
  gboolean msg_tied_seen;
  gboolean floating_seen;
} DupCheckState;

static gboolean
_check_dup_variable(FilterXVariable *variable, gpointer user_data)
{
  DupCheckState *state = (DupCheckState *) user_data;

  state->num_variables_seen++;
  if (variable->handle == state->msg_tied_handle)
    {
      state->msg_tied_seen = TRUE;
      cr_assert(filterx_boolean_get_value(variable->value));
    }
  else if (variable->handle == state->floating_handle)
    {
      state->floating_seen = TRUE;
      cr_assert_not(filterx_boolean_get_value(variable->value));
    }
  else
    cr_assert(FALSE, "unexpected variable in duplicated scope");

  return TRUE;
}

Test(filterx_scope, test_scope_dup_flattens_parent_chain_into_an_independent_copy)
{
  FilterXVariableHandle msg_tied_handle = filterx_map_varname_to_handle("$var", FX_VAR_MESSAGE_TIED);
  FilterXVariableHandle floating_handle = filterx_map_varname_to_handle("floatvar", FX_VAR_DECLARED_FLOATING);
  FilterXVariableHandle handles[] = { msg_tied_handle, floating_handle };
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *parent = filterx_scope_new(NULL, l);
  LogMessage *msg = log_msg_new_empty();
  filterx_scope_set_message(parent, msg);

  FilterXVariable *v = filterx_scope_register_variable(parent, FX_VAR_MESSAGE_TIED, msg_tied_handle, 0);
  FilterXObject *o = filterx_boolean_new(TRUE);
  filterx_scope_set_variable(parent, v, &o, TRUE);
  filterx_object_unref(o);

  v = filterx_scope_register_variable(parent, FX_VAR_DECLARED_FLOATING, floating_handle, 1);
  o = filterx_boolean_new(FALSE);
  filterx_scope_set_variable(parent, v, &o, TRUE);
  filterx_object_unref(o);

  /* child never touches either variable directly, they only live in the parent scope */
  FilterXScope *child = filterx_scope_new(parent, l);

  FilterXScope *dup = filterx_scope_dup(child, _retain_ref, NULL);

  /* tear down the originals: the dup must be fully independent of them */
  filterx_scope_free(child);
  filterx_scope_free(parent);
  filterx_scope_variable_layout_free(l);
  log_msg_unref(msg);

  DupCheckState state =
  {
    .msg_tied_handle = msg_tied_handle,
    .floating_handle = floating_handle,
  };
  cr_assert(filterx_scope_foreach_variable_readonly(dup, _check_dup_variable, &state));
  cr_assert_eq(state.num_variables_seen, 2);
  cr_assert(state.msg_tied_seen);
  cr_assert(state.floating_seen);

  filterx_scope_free_dup(dup);
}

typedef struct _AncestorDupCheckState
{
  FilterXVariableHandle msg_tied_handle;
  FilterXVariableHandle declared_handle;
  FilterXVariableHandle plain_handle;
  gint num_variables_seen;
  gboolean msg_tied_seen;
  gboolean declared_seen;
} AncestorDupCheckState;

static gboolean
_check_ancestor_dup_variable(FilterXVariable *variable, gpointer user_data)
{
  AncestorDupCheckState *state = (AncestorDupCheckState *) user_data;

  state->num_variables_seen++;
  cr_assert_neq(variable->handle, state->plain_handle,
                "a plain (undeclared) floating variable must not survive scope duplication");

  if (variable->handle == state->msg_tied_handle)
    state->msg_tied_seen = TRUE;
  else if (variable->handle == state->declared_handle)
    state->declared_seen = TRUE;

  return TRUE;
}

/* @child's own block never references any of @parent's variables (a
 * different, unrelated filterx{} block chained before it) -- exercising
 * filterx_scope_dup()'s two-scope split: @child's own layout must stay
 * exactly its own (so scope_var_idx values compiled against it remain
 * valid), while message-tied/declared floating variables inherited from
 * @parent land in a separate, flattened ancestor scope reachable only via
 * handle-based lookup. A plain (undeclared) floating variable in @parent
 * must not survive the boundary at all. */
Test(filterx_scope, test_scope_dup_splits_into_own_and_flattened_ancestor_scopes)
{
  FilterXVariableHandle msg_tied_handle = filterx_map_varname_to_handle("$var", FX_VAR_MESSAGE_TIED);
  FilterXVariableHandle declared_handle = filterx_map_varname_to_handle("declaredvar", FX_VAR_DECLARED_FLOATING);
  FilterXVariableHandle plain_handle = filterx_map_varname_to_handle("plainvar", FX_VAR_FLOATING);
  FilterXVariableHandle parent_handles[] = { msg_tied_handle, declared_handle, plain_handle };
  FilterXScopeVariableLayout *parent_layout =
    filterx_scope_variable_layout_new_from_handles(parent_handles, G_N_ELEMENTS(parent_handles));

  FilterXScope *parent = filterx_scope_new(NULL, parent_layout);
  LogMessage *msg = log_msg_new_empty();
  filterx_scope_set_message(parent, msg);

  FilterXVariable *v = filterx_scope_register_variable(parent, FX_VAR_MESSAGE_TIED, msg_tied_handle, 0);
  FilterXObject *o = filterx_boolean_new(TRUE);
  filterx_scope_set_variable(parent, v, &o, TRUE);
  filterx_object_unref(o);

  v = filterx_scope_register_variable(parent, FX_VAR_DECLARED_FLOATING, declared_handle, 1);
  o = filterx_boolean_new(TRUE);
  filterx_scope_set_variable(parent, v, &o, TRUE);
  filterx_object_unref(o);

  v = filterx_scope_register_variable(parent, FX_VAR_FLOATING, plain_handle, 2);
  o = filterx_boolean_new(TRUE);
  filterx_scope_set_variable(parent, v, &o, TRUE);
  filterx_object_unref(o);

  FilterXVariableHandle own_handle = filterx_map_varname_to_handle("ownvar", FX_VAR_DECLARED_FLOATING);
  FilterXVariableHandle child_handles[] = { own_handle };
  FilterXScopeVariableLayout *child_layout =
    filterx_scope_variable_layout_new_from_handles(child_handles, G_N_ELEMENTS(child_handles));
  FilterXScope *child = filterx_scope_new(parent, child_layout);

  FilterXScope *dup = filterx_scope_dup(child, _retain_ref, NULL);

  /* tear down the originals: the dup must be fully independent of them */
  filterx_scope_free(child);
  filterx_scope_variable_layout_free(child_layout);
  filterx_scope_free(parent);
  filterx_scope_variable_layout_free(parent_layout);
  log_msg_unref(msg);

  /* the "own" scope keeps @child's exact layout (a single slot) */
  cr_assert_eq(dup->layout->num_variables, 1);

  /* the inherited variables live in a separate ancestor scope */
  cr_assert_not_null(dup->parent_scope);

  AncestorDupCheckState state =
  {
    .msg_tied_handle = msg_tied_handle,
    .declared_handle = declared_handle,
    .plain_handle = plain_handle,
  };
  cr_assert(filterx_scope_foreach_variable_readonly(dup, _check_ancestor_dup_variable, &state));
  cr_assert_eq(state.num_variables_seen, 2);
  cr_assert(state.msg_tied_seen);
  cr_assert(state.declared_seen);

  filterx_scope_free_dup(dup);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_scope, .init = setup, .fini = teardown);

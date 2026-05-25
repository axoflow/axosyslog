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

Test(filterx_scope, test_cannot_register_variable_in_write_protected_scope, .signal=SIGABRT)
{
  FilterXVariableHandle handles[] = {filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING)};
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *s = filterx_scope_new(NULL, l);
  filterx_scope_write_protect(s);

  FilterXVariableHandle var = filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING);
  filterx_scope_register_variable(s, FX_VAR_DECLARED_FLOATING, var, 0);

  filterx_scope_free(s);
  filterx_scope_variable_layout_free(l);
}

Test(filterx_scope, test_cannot_unset_variable_in_write_protected_scope, .signal=SIGABRT)
{
  FilterXVariableHandle handles[] = {filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING)};
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *s = filterx_scope_new(NULL, l);

  FilterXVariableHandle v_handle = filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING);
  FilterXVariable *v = filterx_scope_register_variable(s, FX_VAR_DECLARED_FLOATING, v_handle, 0);

  filterx_scope_write_protect(s);

  filterx_scope_unset_variable(s, v);

  filterx_scope_free(s);
  filterx_scope_variable_layout_free(l);
}

Test(filterx_scope, test_cannot_change_variable_in_write_protected_scope, .signal=SIGABRT)
{
  FilterXVariableHandle handles[] = {filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING)};
  FilterXScopeVariableLayout *l = filterx_scope_variable_layout_new_from_handles(handles, G_N_ELEMENTS(handles));

  FilterXScope *s = filterx_scope_new(NULL, l);

  FilterXVariableHandle v_handle = filterx_map_varname_to_handle("var", FX_VAR_DECLARED_FLOATING);
  FilterXVariable *v = filterx_scope_register_variable(s, FX_VAR_DECLARED_FLOATING, v_handle, 0);

  filterx_scope_write_protect(s);
  FilterXObject *value = filterx_boolean_new(TRUE);
  filterx_scope_set_variable(s, v, &value, TRUE);
  filterx_object_unref(value);

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

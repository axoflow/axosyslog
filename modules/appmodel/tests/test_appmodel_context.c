/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 Balazs Scheidler <balazs.scheidler@balabit.com>
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
#include "appmodel.h"

static AppModelContext *ac;

static void
startup(void)
{
  ac = appmodel_context_new();
}

static void
teardown(void)
{
  appmodel_context_free(ac);
  ac = NULL;
}

Test(appmodel_context, register_application_makes_the_app_available)
{
  appmodel_context_register_object(ac, &application_new("foobar", "*")->super);
  appmodel_context_lookup_object(ac, APPLICATION_TYPE_NAME, "foobar", "*");
}

static void
_foreach_app(AppModelObject *app, gpointer user_data)
{
  GString *result = (GString *) user_data;

  g_string_append(result, app->name);
}

Test(appmodel_context, iter_applications_enumerates_apps_without_asterisk)
{
  GString *result = g_string_sized_new(128);

  appmodel_context_register_object(ac, &application_new("foo", "port514")->super);
  appmodel_context_register_object(ac, &application_new("bar", "port514")->super);
  appmodel_context_register_object(ac, &application_new("baz", "port514")->super);
  appmodel_context_iter_objects(ac, APPLICATION_TYPE_NAME, _foreach_app, result);
  cr_assert_str_eq(result->str, "foobarbaz");
  g_string_free(result, TRUE);
}

Test(appmodel_context, iter_applications_enumerates_apps_in_the_order_of_registration)
{
  GString *result = g_string_sized_new(128);

  appmodel_context_register_object(ac, &application_new("baz", "port514")->super);
  appmodel_context_register_object(ac, &application_new("bar", "port514")->super);
  appmodel_context_register_object(ac, &application_new("foo", "port514")->super);
  appmodel_context_iter_objects(ac, APPLICATION_TYPE_NAME, _foreach_app, result);
  cr_assert_str_eq(result->str, "bazbarfoo");
  g_string_free(result, TRUE);
}

TestSuite(appmodel_context, .init = startup, .fini = teardown);

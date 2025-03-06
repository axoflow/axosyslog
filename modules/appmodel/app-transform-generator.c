/*
 * Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "app-object-generator.h"
#include "appmodel.h"

/* app-transform() */

typedef struct _AppTransformGenerator
{
  AppObjectGenerator super;
  const gchar *topic;
  const gchar *filterx_app_variable;
  GList *included_transforms;
  GList *excluded_transforms;
  GString *block;
} AppTransformGenerator;

static gboolean
_parse_transforms_arg(AppTransformGenerator *self, CfgArgs *args, const gchar *reference)
{
  self->topic = cfg_args_get(args, "topic");
  if (!self->topic)
    self->topic = "default";
  return TRUE;
}

static gboolean
_parse_filterx_app_variable(AppTransformGenerator *self, CfgArgs *args, const gchar *reference)
{
  self->filterx_app_variable = cfg_args_get(args, "filterx-app-variable");
  if (!self->filterx_app_variable)
    {
      msg_error("app-transform() requires a filterx-app-variable() argument",
                evt_tag_str("reference", reference));
      return FALSE;
    }
  return TRUE;
}

static gboolean
_parse_include_transforms_arg(AppTransformGenerator *self, CfgArgs *args, const gchar *reference)
{
  const gchar *v = cfg_args_get(args, "include-transforms");
  if (!v)
    return TRUE;
  return cfg_process_list_of_literals(v, &self->included_transforms);
}

static gboolean
_parse_exclude_transforms_arg(AppTransformGenerator *self, CfgArgs *args, const gchar *reference)
{
  const gchar *v = cfg_args_get(args, "exclude-transforms");
  if (!v)
    return TRUE;
  return cfg_process_list_of_literals(v, &self->excluded_transforms);
}

static gboolean
app_transform_generator_parse_arguments(AppObjectGenerator *s, CfgArgs *args, const gchar *reference)
{
  AppTransformGenerator *self = (AppTransformGenerator *) s;
  g_assert(args != NULL);

  if (!_parse_transforms_arg(self, args, reference))
    return FALSE;

  if (!_parse_filterx_app_variable(self, args, reference))
    return FALSE;

  if (!_parse_include_transforms_arg(self, args, reference))
    return FALSE;

  if (!_parse_exclude_transforms_arg(self, args, reference))
    return FALSE;

  if (!app_object_generator_parse_arguments_method(&self->super, args, reference))
    return FALSE;

  return TRUE;
}

static void
_generate_steps(AppTransformGenerator *self, GList *steps)
{
  for (GList *l = steps; l; l = l->next)
    {
      TransformStep *step = l->data;
      g_string_append_printf(self->block, "        # step: %s\n", step->name);
      g_string_append_printf(self->block, "        %s\n", step->expr);
    }
}

static gboolean
_is_transform_included(AppTransformGenerator *self, const gchar *name)
{
  if (!self->included_transforms)
    return TRUE;
  return cfg_is_literal_in_list_of_literals(self->included_transforms, name);
}

static gboolean
_is_transform_excluded(AppTransformGenerator *self, const gchar *name)
{
  if (!self->excluded_transforms)
    return FALSE;
  return cfg_is_literal_in_list_of_literals(self->excluded_transforms, name);
}

static void
_generate_app_transform(Transformation *transformation, gpointer user_data)
{
  AppTransformGenerator *self = (AppTransformGenerator *) user_data;

  if (strcmp(transformation->super.instance, self->topic) != 0)
    return;

  if (!app_object_generator_is_application_included(&self->super, transformation->super.name))
    return;

  if (app_object_generator_is_application_excluded(&self->super, transformation->super.name))
    return;

  g_string_append_printf(self->block, "\n#Start Application %s\n", transformation->super.name);
  g_string_append_printf(self->block,     "    case '%s':\n", transformation->super.name);
  for (GList *l = transformation->transforms; l; l = l->next)
    {
      Transform *transform = l->data;

      if (!_is_transform_included(self, transform->name) || _is_transform_excluded(self, transform->name))
        continue;

      _generate_steps(self, transform->steps);
    }
  g_string_append(self->block,            "        break;\n");
  g_string_append_printf(self->block, "\n#End Application %s\n", transformation->super.name);
}


static void
app_transform_generate_config(AppObjectGenerator *s, GlobalConfig *cfg, GString *result)
{
  AppTransformGenerator *self = (AppTransformGenerator *) s;

  self->block = result;
  g_string_append_printf(result, "## app-transform(topic(%s))\n"
                         "channel {\n"
                         "    filterx {\n"
                         "        switch (%s) {\n", self->topic, self->filterx_app_variable);
  appmodel_iter_transformations(cfg, _generate_app_transform, self);
  g_string_append(result, "        }\n"
                  "    };\n"
                  "}");
  self->block = NULL;
}

static void
_free(CfgBlockGenerator *s)
{
  AppTransformGenerator *self = (AppTransformGenerator *) s;

  g_list_free_full(self->included_transforms, g_free);
  g_list_free_full(self->excluded_transforms, g_free);
  app_object_generator_free_method(s);
}

CfgBlockGenerator *
app_transform_generator_new(gint context, const gchar *name)
{
  AppTransformGenerator *self = g_new0(AppTransformGenerator, 1);

  app_object_generator_init_instance(&self->super, context, name);
  self->super.parse_arguments = app_transform_generator_parse_arguments;
  self->super.generate_config = app_transform_generate_config;
  self->super.super.free_fn = _free;
  return &self->super.super;
}

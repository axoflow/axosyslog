/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
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

#include <criterion/criterion.h>
#include <stdio.h>
#include <glib.h>
#include <stdarg.h>
#include "libtest/config_parse_lib.h"

#include "app-transform-generator.h"
#include "apphook.h"
#include "plugin-types.h"

static CfgBlockGenerator *app_transform;
static GString *result = NULL;

void
_register_transformation(const char *appmodel)
{
  cr_assert(parse_config(appmodel, LL_CONTEXT_ROOT, NULL, NULL),
            "Parsing the given configuration failed: %s", appmodel);
}

static CfgBlockGenerator *
_construct_app_transform(void)
{
  return app_transform_generator_new(LL_CONTEXT_PARSER, "app-transform");
}

/* Function to create a step block */
GString *create_step(const char *stepname, const char *codeblock)
{
  GString *step = g_string_new(NULL);
  g_string_append_printf(step, "    step[%s] {\n        %s\n    };\n", stepname, codeblock);
  return step;
}

/* Function to create a transform block with multiple steps */
GString *create_transform(const char *transformname, ...)
{
  GString *transform = g_string_new(NULL);
  g_string_append_printf(transform, "  transform[%s]{\n", transformname);

  va_list args;
  va_start(args, transformname);
  GString *step;
  while ((step = va_arg(args, GString *)) != NULL)
    {
      g_string_append(transform, step->str);
      g_string_free(step, TRUE);
    }
  va_end(args);

  g_string_append(transform, "  };\n");
  return transform;
}

/* Function to create a transformation block with multiple transforms */
GString *create_transformation(const char *name, const char *topic, ...)
{
  GString *transformation = g_string_new(NULL);
  g_string_append_printf(transformation, "transformation %s[%s] {\n", name, topic);

  va_list args;
  va_start(args, topic);
  GString *transform;
  while ((transform = va_arg(args, GString *)) != NULL)
    {
      g_string_append(transformation, transform->str);
      g_string_free(transform, TRUE);
    }
  va_end(args);

  g_string_append(transformation, "};\n");
  return transformation;
}

static GString *
_remove_comments(const GString *str)
{
  GString *prune = g_string_new("");
  gboolean is_inside_comment = FALSE;

  for (gint i=0; i < str->len; ++i)
    {
      const gchar curr = str->str[i];
      const gboolean is_comment_start = '#' == curr;
      const gboolean is_comment_end = curr == '\n';
      const gboolean does_comment_continue_next_line = is_comment_end && (i+1) < str->len && '#' == str->str[i+1];

      if (is_comment_start || does_comment_continue_next_line)
        {
          is_inside_comment = TRUE;
        }

      if (!is_inside_comment)
        {
          g_string_append_c(prune, str->str[i]);
        }

      if (is_comment_end)
        {
          is_inside_comment = FALSE;
        }

    }

  return prune;
}

static void
_app_transform_generate_with_args(CfgArgs *args)
{
  GString *tmp = g_string_new("");
  cfg_block_generator_generate(app_transform, configuration, args, tmp, "dummy-reference");
  g_string_free(result, TRUE);
  result = _remove_comments(tmp);
  g_string_free(tmp, TRUE);
  cfg_args_unref(args);
}

static void
_app_transform_generate_with_fx_app_var(const gchar *topic, CfgArgs *args)
{
  cfg_args_set(args, "topic", topic);
  cfg_args_set(args, "filterx-app-variable", "meta");
  _app_transform_generate_with_args(args);
}

static void
_app_transform_generate(const gchar *topic)
{
  _app_transform_generate_with_fx_app_var(topic, cfg_args_new());
}

static bool _config_contains(const gchar *substr)
{
  return strstr(result->str, substr) != NULL;
}

static void _assert_config_contains(const gchar *substr)
{
  cr_assert(_config_contains(substr), "config element not found: \"%s\"", substr);
}

static void _assert_config_not_contains(const gchar *substr)
{
  cr_assert(!_config_contains(substr), "config element unexpectedly found: \"%s\"", substr);
}

static void
_assert_config_is_valid(const gchar *topic, const gchar *filterx_app_variable, const gchar *varargs)
{
  const gchar *config_fmt = ""
                            "parser p_test {\n"
                            "    app-transform(topic(\"%s\") filterx-app-variable(\"%s\") %s);\n"
                            "};\n";
  gchar *config;

  config = g_strdup_printf(config_fmt, filterx_app_variable, topic, varargs ? : "");
  cr_assert(parse_config(config, LL_CONTEXT_ROOT, NULL, NULL),
            "Parsing the given configuration failed: %s", config);
  g_free(config);
}


static CfgArgs *
_build_cfg_args(const gchar *key, const gchar *value, ...)
{
  CfgArgs *args = cfg_args_new();
  va_list va;

  va_start(va, value);
  while (key)
    {
      cfg_args_set(args, key, value);
      key = va_arg(va, gchar *);
      if (!key)
        break;
      value = va_arg(va, gchar *);
    }
  va_end(va);
  return args;
}

static void
startup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
  cfg_load_module(configuration, "appmodel");
  app_transform = _construct_app_transform();
  result = g_string_new("");
}

static void
teardown(void)
{
  g_string_free(result, TRUE);
  app_shutdown();
  cfg_block_generator_unref(app_transform);
}

Test(app_transform_generator, all_steps_are_added_to_config)
{
  GString *transform1 = create_transformation("my-transformation", "my-topic",
                                              create_transform("transform-1",
                                                  create_step("step-1-1", "$MESSAGE = \"first step here\";"),
                                                  create_step("step-1-2", "$MESSAGE += \"second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform1->str);
  g_string_free(transform1, TRUE);

  _app_transform_generate("my-topic");

  _assert_config_contains("case 'my-transformation':\n");
  _assert_config_contains("$MESSAGE = \"first step here\";\n");
  _assert_config_contains("$MESSAGE += \"second step here\";\n");
  _assert_config_is_valid("my_topic", "meta", "");
}

Test(app_transform_generator, all_transforms_are_added_to_config)
{
  GString *transform1 = create_transformation("my-transformation", "my-topic",
                                              create_transform("transform-1",
                                                  create_step("step-1-1", "$MESSAGE = \"first step here\";"),
                                                  create_step("step-1-2", "$MESSAGE += \"second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-2",
                                                  create_step("step-2-1", "$MESSAGE = \"2/first step here\";"),
                                                  create_step("step-2-2", "$MESSAGE += \"2/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform1->str);

  _app_transform_generate("my-topic");

  _assert_config_contains("case 'my-transformation':\n");
  _assert_config_contains("$MESSAGE = \"first step here\";\n");
  _assert_config_contains("$MESSAGE += \"second step here\";\n");
  _assert_config_contains("$MESSAGE = \"2/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"2/second step here\";\n");
  _assert_config_is_valid("my_topic", "meta", "");
  g_string_free(transform1, TRUE);
}

Test(app_transform_generator, add_multiple_tranformations_with_particular_topic)
{
  GString *transform1 = create_transformation("my-transformation", "my-topic",
                                              create_transform("transform-1",
                                                  create_step("step-1-1", "$MESSAGE = \"1/1/first step here\";"),
                                                  create_step("step-1-2", "$MESSAGE += \"1/1/second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-2",
                                                  create_step("step-2-1", "$MESSAGE = \"1/2/first step here\";"),
                                                  create_step("step-2-2", "$MESSAGE += \"1/2/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform1->str);

  GString *transform2 = create_transformation("my-transformation2", "my-topic",
                                              create_transform("transform-2/1",
                                                  create_step("step-2-1-1", "$MESSAGE = \"2/1/first step here\";"),
                                                  create_step("step-2-1-2", "$MESSAGE += \"2/1/second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-2/2",
                                                  create_step("step-2-2-1", "$MESSAGE = \"2/2/first step here\";"),
                                                  create_step("step-2-2-2", "$MESSAGE += \"2/2/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform2->str);

  _app_transform_generate("my-topic");

  _assert_config_contains("case 'my-transformation':\n");
  _assert_config_contains("$MESSAGE = \"1/1/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"1/1/second step here\";\n");
  _assert_config_contains("$MESSAGE = \"1/2/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"1/2/second step here\";\n");
  _assert_config_contains("case 'my-transformation2':\n");
  _assert_config_contains("$MESSAGE = \"2/1/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"2/1/second step here\";\n");
  _assert_config_contains("$MESSAGE = \"2/2/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"2/2/second step here\";\n");
  _assert_config_is_valid("my_topic", "meta", "");
  g_string_free(transform1, TRUE);
  g_string_free(transform2, TRUE);

}

Test(app_transform_generator, do_not_add_off_topic_transformations)
{
  GString *transform1 = create_transformation("my-transformation", "my-topic",
                                              create_transform("transform-1",
                                                  create_step("step-1-1", "$MESSAGE = \"1/1/first step here\";"),
                                                  create_step("step-1-2", "$MESSAGE += \"1/1/second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-2",
                                                  create_step("step-2-1", "$MESSAGE = \"1/2/first step here\";"),
                                                  create_step("step-2-2", "$MESSAGE += \"1/2/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform1->str);

  GString *transform2 = create_transformation("my-transformation2", "off-topic",
                                              create_transform("transform-2/1",
                                                  create_step("step-2-1-1", "$MESSAGE = \"2/1/first step here\";"),
                                                  create_step("step-2-1-2", "$MESSAGE += \"2/1/second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-2/2",
                                                  create_step("step-2-2-1", "$MESSAGE = \"2/2/first step here\";"),
                                                  create_step("step-2-2-2", "$MESSAGE += \"2/2/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform2->str);

  _app_transform_generate("my-topic");

  _assert_config_contains("case 'my-transformation':\n");
  _assert_config_contains("$MESSAGE = \"1/1/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"1/1/second step here\";\n");
  _assert_config_contains("$MESSAGE = \"1/2/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"1/2/second step here\";\n");
  _assert_config_not_contains("case 'my-transformation2':");
  _assert_config_not_contains("$MESSAGE = \"2/1/first step here\";");
  _assert_config_not_contains("$MESSAGE += \"2/1/second step here\";");
  _assert_config_not_contains("$MESSAGE = \"2/2/first step here\";");
  _assert_config_not_contains("$MESSAGE += \"2/2/second step here\";");
  _assert_config_is_valid("my_topic", "meta", "");
  g_string_free(transform1, TRUE);
  g_string_free(transform2, TRUE);
}

Test(app_transform_generator, include_transforms)
{
  GString *transform1 = create_transformation("my-transformation", "my-topic",
                                              create_transform("transform-1",
                                                  create_step("step-1-1", "$MESSAGE = \"1/1/first step here\";"),
                                                  create_step("step-1-2", "$MESSAGE += \"1/1/second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-2",
                                                  create_step("step-2-1", "$MESSAGE = \"1/2/first step here\";"),
                                                  create_step("step-2-2", "$MESSAGE += \"1/2/second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-3",
                                                  create_step("step-3-1", "$MESSAGE = \"1/3/first step here\";"),
                                                  create_step("step-3-2", "$MESSAGE += \"1/3/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform1->str);

  _app_transform_generate_with_fx_app_var("my-topic", _build_cfg_args("include-transforms", "transform-2", NULL));

  _assert_config_contains("case 'my-transformation':\n");
  _assert_config_not_contains("$MESSAGE = \"1/1/first step here\";\n");
  _assert_config_not_contains("$MESSAGE += \"1/1/second step here\";\n");
  _assert_config_contains("$MESSAGE = \"1/2/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"1/2/second step here\";\n");
  _assert_config_not_contains("$MESSAGE = \"1/3/first step here\";\n");
  _assert_config_not_contains("$MESSAGE += \"1/3/second step here\";\n");
  _assert_config_is_valid("my_topic", "meta", "");
  g_string_free(transform1, TRUE);
}

Test(app_transform_generator, exclude_transforms)
{
  GString *transform1 = create_transformation("my-transformation", "my-topic",
                                              create_transform("transform-1",
                                                  create_step("step-1-1", "$MESSAGE = \"1/1/first step here\";"),
                                                  create_step("step-1-2", "$MESSAGE += \"1/1/second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-2",
                                                  create_step("step-2-1", "$MESSAGE = \"1/2/first step here\";"),
                                                  create_step("step-2-2", "$MESSAGE += \"1/2/second step here\";"),
                                                  NULL
                                                              ),
                                              create_transform("transform-3",
                                                  create_step("step-3-1", "$MESSAGE = \"1/3/first step here\";"),
                                                  create_step("step-3-2", "$MESSAGE += \"1/3/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform1->str);

  _app_transform_generate_with_fx_app_var("my-topic", _build_cfg_args("exclude-transforms", "transform-1", NULL));

  _assert_config_contains("case 'my-transformation':\n");
  _assert_config_not_contains("$MESSAGE = \"1/1/first step here\";\n");
  _assert_config_not_contains("$MESSAGE += \"1/1/second step here\";\n");
  _assert_config_contains("$MESSAGE = \"1/2/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"1/2/second step here\";\n");
  _assert_config_contains("$MESSAGE = \"1/3/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"1/3/second step here\";\n");
  _assert_config_is_valid("my_topic", "meta", "");
  g_string_free(transform1, TRUE);
}


Test(app_transform_generator, default_topic)
{
  GString *transform1 = create_transformation("my-transformation", "default",
                                              create_transform("transform-1",
                                                  create_step("step-1-1", "$MESSAGE = \"1/1/first step here\";"),
                                                  create_step("step-1-2", "$MESSAGE += \"1/1/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform1->str);

  GString *transform2 = create_transformation("my-transformation2", "my-topic",
                                              create_transform("transform-2/1",
                                                  create_step("step-2-1-1", "$MESSAGE = \"2/1/first step here\";"),
                                                  create_step("step-2-1-2", "$MESSAGE += \"2/1/second step here\";"),
                                                  NULL
                                                              ),
                                              NULL
                                             );
  _register_transformation(transform2->str);

  //use implicite default topic here
  CfgArgs *args = cfg_args_new();
  cfg_args_set(args, "filterx-app-variable", "meta"); // required argument added manually
  _app_transform_generate_with_args(args);

  _assert_config_contains("case 'my-transformation':\n");
  _assert_config_contains("$MESSAGE = \"1/1/first step here\";\n");
  _assert_config_contains("$MESSAGE += \"1/1/second step here\";\n");
  _assert_config_not_contains("case 'my-transformation2':");
  _assert_config_not_contains("$MESSAGE = \"2/1/first step here\";");
  _assert_config_not_contains("$MESSAGE += \"2/1/second step here\";");

  _assert_config_is_valid("my_topic", "meta", "");
  g_string_free(transform1, TRUE);
  g_string_free(transform2, TRUE);
}

TestSuite(app_transform_generator, .init = startup, .fini = teardown);

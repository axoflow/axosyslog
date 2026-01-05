/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "func-cache-json-file.h"
#include "filterx/json-repr.h"
#include "filterx/object-string.h"
#include "filterx/filterx-sequence.h"
#include "filterx/filterx-mapping.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-stashed-object.h"
#include "scratch-buffers.h"
#include "file-monitor.h"
#include "mainloop.h"
#include "mainloop-worker.h"

#include "compat/json.h"

#include <stdio.h>
#include <errno.h>
#include <glib.h>

#define FILTERX_FUNC_CACHE_JSON_FILE_USAGE "Usage: cache_json_file(\"/path/to/file.json\", default_value=[dict])"

#define CACHE_JSON_FILE_ERROR cache_json_file_error_quark()

enum FilterXFunctionCacheJsonFileError
{
  CACHE_JSON_FILE_ERROR_FILE_OPEN_ERROR,
  CACHE_JSON_FILE_ERROR_FILE_READ_ERROR,
  CACHE_JSON_FILE_ERROR_JSON_PARSE_ERROR,
  CACHE_JSON_FILE_ERROR_JSON_FREEZE_ERROR,
};

static GQuark
cache_json_file_error_quark(void)
{
  return g_quark_from_static_string("filterx-function-cache-json-file-error-quark");
}


typedef struct FilterXFunctionCacheJsonFile_
{
  FilterXFunction super;
  gchar *filepath;
  FilterXStashedObject *stashed_object;
  FileMonitor *file_monitor;
  FilterXExpr *default_value;
} FilterXFunctionCacheJsonFile;

static gchar *
_extract_filepath(FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_CACHE_JSON_FILE_USAGE);
      return NULL;
    }

  gsize filepath_len;
  const gchar *filepath = filterx_function_args_get_literal_string(args, 0, &filepath_len);
  if (!filepath)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be string literal. " FILTERX_FUNC_CACHE_JSON_FILE_USAGE);
      return NULL;
    }

  return g_strdup(filepath);
}

static FilterXObject *
_load_json_file(const gchar *filepath, GError **error)
{
  gchar *content;
  gsize length;
  GError *local_error = NULL;

  if (!g_file_get_contents(filepath, &content, &length, &local_error))
    {
      g_set_error(error, CACHE_JSON_FILE_ERROR, CACHE_JSON_FILE_ERROR_FILE_OPEN_ERROR,
                  "failed to open file: %s (%s)", filepath, local_error->message);
      g_clear_error(&local_error);
      return NULL;
    }

  FilterXObject *result = filterx_object_from_json(content, length, &local_error);
  g_free(content);
  if (!result)
    {
      g_set_error(error, CACHE_JSON_FILE_ERROR, CACHE_JSON_FILE_ERROR_JSON_PARSE_ERROR,
                  "failed to parse JSON file: %s (%s)", filepath, local_error->message);
      g_clear_error(&local_error);
      return NULL;
    }

  if (!filterx_object_is_type_or_ref(result, &FILTERX_TYPE_NAME(mapping)) &&
      !filterx_object_is_type_or_ref(result, &FILTERX_TYPE_NAME(sequence)))
    {
      g_set_error(error, CACHE_JSON_FILE_ERROR, CACHE_JSON_FILE_ERROR_JSON_PARSE_ERROR,
                  "JSON file must contain an object or an array");
      filterx_object_unref(result);
      return NULL;
    }
  return result;
}

static gboolean
_cache_json_file_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionCacheJsonFile *self = (FilterXFunctionCacheJsonFile *) s;

  if (self->default_value)
    {
      if (!filterx_expr_is_literal(self->default_value))
        {
          msg_error("The default_value argument for cache_json_file() has to be a literal",
                    filterx_expr_format_location_tag(s));
          return FALSE;
        }
    }
  return filterx_function_init_method(&self->super, cfg);
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionCacheJsonFile *self = (FilterXFunctionCacheJsonFile *) s;
  FilterXObject *result = NULL;

  FilterXStashedObject *stashed_object = g_atomic_pointer_get(&self->stashed_object);
  if (stashed_object)
    {
      filterx_stashed_object_ref(stashed_object);
      result = filterx_object_ref(stashed_object->object);
      filterx_stashed_object_unref(stashed_object);
    }
  if (!result)
    return filterx_expr_eval_typed(self->default_value);
  else
    return result;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionCacheJsonFile *self = (FilterXFunctionCacheJsonFile *) s;

  filterx_expr_unref(self->default_value);
  filterx_stashed_object_unref(self->stashed_object);
  g_free(self->filepath);
  if (self->file_monitor)
    {
      file_monitor_stop(self->file_monitor);
      file_monitor_free(self->file_monitor);
    }
  filterx_function_free_method(&self->super);
}

gboolean
_load_json_file_version(FilterXFunctionCacheJsonFile *self, GError **error)
{
  FilterXEnvironment json_env;
  FilterXEvalContext json_reload_context;
  gboolean result = FALSE;

  filterx_env_init(&json_env);
  filterx_eval_begin_restricted_context(&json_reload_context, &json_env);
  FilterXObject *json = _load_json_file(self->filepath, error);
  filterx_eval_end_restricted_context(&json_reload_context);

  if (json)
    {
      FilterXStashedObject *new_value = filterx_stash_object(json, &json_env);
      FilterXStashedObject *old_value = g_atomic_pointer_exchange(&self->stashed_object, new_value);
      filterx_stashed_object_unref(old_value);
      result = TRUE;
    }
  filterx_env_clear(&json_env);
  return result;
}

gboolean
_file_monitor_callback(const FileMonitorEvent *event, gpointer user_data)
{
  FilterXFunctionCacheJsonFile *self = user_data;
  if (event->event == DELETED)
    {
      msg_error("FilterX: Backend file of cache-json-file was deleted, keeping current json version.",
                evt_tag_str("file_name", self->filepath));
      return TRUE;
    }

  main_loop_assert_main_thread();

  GError *error = NULL;
  if (!_load_json_file_version(self, &error) && error)
    {
      msg_error("FilterX: Error while loading json file, keeping current json version.",
                evt_tag_str("file_name", self->filepath),
                evt_tag_str("error_message", error->message));
      g_clear_error(&error);
    }

  return TRUE;
}

gboolean
_cache_json_file_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionCacheJsonFile *self = (FilterXFunctionCacheJsonFile *) s;

  return filterx_expr_visit(s, &self->default_value, f, user_data);
}

static FilterXExpr *
_extract_cache_json_file_default_value_dict(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *default_value = filterx_function_args_get_named_expr(args, "default_value");

  if (!default_value)
    return NULL;

  if (!filterx_expr_is_literal_dict(default_value))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "default_value argument must a literal dict. " FILTERX_FUNC_CACHE_JSON_FILE_USAGE);
      return NULL;
    }

  return default_value;
}

static gboolean
_try_load_from_file_with_default(FilterXFunctionCacheJsonFile *self, GError **error)
{
  if (_load_json_file_version(self, error))
    return TRUE;

  if (!self->default_value)
    return FALSE;

  g_clear_error(error);
  return TRUE;
}

FilterXExpr *
filterx_function_cache_json_file_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionCacheJsonFile *self = g_new0(FilterXFunctionCacheJsonFile, 1);
  filterx_function_init_instance(&self->super, "cache_json_file", FXE_WORLD);

  self->super.super.eval = _eval;
  self->super.super.init = _cache_json_file_init;
  self->super.super.walk_children = _cache_json_file_walk;
  self->super.super.free_fn = _free;

  self->default_value = _extract_cache_json_file_default_value_dict(args, error);
  if (*error)
    goto error;
  self->filepath = _extract_filepath(args, error);
  if (!self->filepath)
    goto error;

  if (!filterx_function_args_check(args, error))
    goto error;

  if (!_try_load_from_file_with_default(self, error))
    goto error;

  if (self->stashed_object)
    {
      filterx_expr_unref(self->default_value);
      self->default_value = NULL;
    }

  self->file_monitor = file_monitor_new(self->filepath);
  file_monitor_add_watch(self->file_monitor, _file_monitor_callback, self);
  file_monitor_start(self->file_monitor);

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

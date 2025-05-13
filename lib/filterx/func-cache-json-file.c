/*
 * Copyright (c) 2024 Attila Szakacs
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "func-cache-json-file.h"
#include "filterx/json-repr.h"
#include "filterx/object-string.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"
#include "file-monitor.h"
#include "mainloop.h"
#include "mainloop-worker.h"

#include "compat/json.h"

#include <stdio.h>
#include <errno.h>
#include <glib.h>

#define FILTERX_FUNC_CACHE_JSON_FILE_USAGE "Usage: cache_json_file(\"/path/to/file.json\")"

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

#define FROZEN_OBJECTS_HISTORY_SIZE 5

typedef struct FilterXFunctionCacheJsonFile_
{
  FilterXFunction super;
  gchar *filepath;
  gpointer cached_json;
  GPtrArray *frozen_objects;
  GPtrArray *frozen_objects_history[FROZEN_OBJECTS_HISTORY_SIZE];
  gint history_index;
  FileMonitor *file_monitor;
} FilterXFunctionCacheJsonFile;

static void
_free_all_frozen_objects(FilterXFunctionCacheJsonFile *self);

static void
_archive_frozen_objects(FilterXFunctionCacheJsonFile *self)
{
  if (!self->frozen_objects)
    return;

  // in case of reload fails somehow
  if (self->history_index >= FROZEN_OBJECTS_HISTORY_SIZE)
    _free_all_frozen_objects(self);

  self->frozen_objects_history[self->history_index] = self->frozen_objects;
  self->history_index++;
  self->frozen_objects = NULL;
}

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
  FILE *file = fopen(filepath, "rb");
  if (!file)
    {
      g_set_error(error, CACHE_JSON_FILE_ERROR, CACHE_JSON_FILE_ERROR_FILE_OPEN_ERROR,
                  "failed to open file: %s (%s)", filepath, g_strerror(errno));
      return NULL;
    }

  struct json_tokener *tokener = json_tokener_new();
  struct json_object *object = NULL;

  gchar buffer[1024];
  while (TRUE)
    {
      gsize bytes_read = fread(buffer, 1, sizeof(buffer), file);
      if (bytes_read <= 0)
        {
          if (ferror(file))
            g_set_error(error, CACHE_JSON_FILE_ERROR, CACHE_JSON_FILE_ERROR_FILE_READ_ERROR,
                        "failed to read file: %s (%s)", filepath, g_strerror(errno));
          else
            g_set_error(error, CACHE_JSON_FILE_ERROR, CACHE_JSON_FILE_ERROR_FILE_READ_ERROR,
                        "failed to read file: %s (unexpected EOF)", filepath);
          break;
        }

      object = json_tokener_parse_ex(tokener, buffer, bytes_read);

      enum json_tokener_error parse_result = json_tokener_get_error(tokener);
      if (parse_result == json_tokener_success)
        break;
      if (parse_result == json_tokener_continue)
        continue;

      g_set_error(error, CACHE_JSON_FILE_ERROR, CACHE_JSON_FILE_ERROR_JSON_PARSE_ERROR,
                  "failed to parse JSON file: %s (%s)", filepath, json_tokener_error_desc(parse_result));
      break;
    }

  FilterXObject *result = NULL;

  if (!object)
    goto exit;

  if (json_object_get_type(object) != json_type_object && json_object_get_type(object) != json_type_array)
    {
      g_set_error(error, CACHE_JSON_FILE_ERROR, CACHE_JSON_FILE_ERROR_JSON_PARSE_ERROR,
                  "JSON file must contain an object or an array");
      json_object_put(object);
      goto exit;
    }

  result = filterx_object_from_json_object(object, error);

exit:
  json_tokener_free(tokener);
  json_object_put(object);
  fclose(file);
  return result;
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionCacheJsonFile *self = (FilterXFunctionCacheJsonFile *) s;
  FilterXObject *cached_json = g_atomic_pointer_get(&self->cached_json);
  return filterx_object_ref(cached_json);
}

static void
_free_all_frozen_objects(FilterXFunctionCacheJsonFile *self)
{
  main_loop_assert_main_thread();
  for (int i = 0; i < FROZEN_OBJECTS_HISTORY_SIZE; i++)
    {
      if (self->frozen_objects_history[i])
        {
          g_ptr_array_unref(self->frozen_objects_history[i]);
          self->frozen_objects_history[i] = NULL;
        }
    }

  self->history_index = 0;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionCacheJsonFile *self = (FilterXFunctionCacheJsonFile *) s;

  g_free(self->filepath);
  if (self->file_monitor)
    {
      file_monitor_stop(self->file_monitor);
      file_monitor_free(self->file_monitor);
    }
  _free_all_frozen_objects(self);
  if (self->frozen_objects)
    g_ptr_array_unref(self->frozen_objects);
  filterx_function_free_method(&self->super);
}

gboolean
_load_json_file_version(FilterXFunctionCacheJsonFile *self, GError **error)
{
  FilterXObject *cached_json = _load_json_file(self->filepath, error);
  if (!cached_json && *error)
    {
      return FALSE;
    }
  _archive_frozen_objects(self);
  self->frozen_objects = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_object_unfreeze_and_free);
  filterx_object_freeze(&cached_json);
  g_ptr_array_add(self->frozen_objects, cached_json);
  g_atomic_pointer_set(&self->cached_json, cached_json);
  return TRUE;
}

// This function may trigger a configuration reload. Ensure proper handling of the configuration on the caller side.
void
_file_monitor_callback(const FileMonitorEvent *event, gpointer user_data)
{
  MainLoop *main_loop = main_loop_get_instance();

  FilterXFunctionCacheJsonFile *self = user_data;
  if (event->event == DELETED)
    {
      msg_error("FilterX: Backend file of cache-json-file was deleted, keeping current json version.",
                evt_tag_str("file_name", self->filepath));
      return;
    }

  main_loop_assert_main_thread();
  if (self->history_index >= FROZEN_OBJECTS_HISTORY_SIZE)
    {
      main_loop_reload_config(main_loop);
      return;
    }

  /* needed for parent tracking of temporary non-frozen objects */
  FilterXEvalContext json_reload_context;
  filterx_eval_begin_compile(&json_reload_context, main_loop_get_current_config(main_loop));

  GError *error = NULL;
  if (!_load_json_file_version(self, &error) && error)
    {
      msg_error("FilterX: Error while loading json file, keeping current json version.",
                evt_tag_str("file_name", self->filepath),
                evt_tag_str("error_message", error->message));
    }

  filterx_eval_end_compile(&json_reload_context);
}

FilterXExpr *
filterx_function_cache_json_file_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionCacheJsonFile *self = g_new0(FilterXFunctionCacheJsonFile, 1);
  filterx_function_init_instance(&self->super, "cache_json_file");

  self->super.super.eval = _eval;
  self->super.super.free_fn = _free;

  self->filepath = _extract_filepath(args, error);
  if (!self->filepath)
    goto error;

  if (!_load_json_file_version(self, error))
    goto error;

  if (!filterx_function_args_check(args, error))
    goto error;

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

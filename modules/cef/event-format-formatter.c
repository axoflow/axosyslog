/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "event-format-formatter.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "scratch-buffers.h"

#define FILTERX_FUNC_EVENT_FORMAT_FORMATTER_USAGE "Usage: %s(msg_dict)"

void
event_format_formatter_context_set_header(EventFormatterContext *ctx, Header *new_header)
{
  ctx->config.header.fields = new_header->fields;
  ctx->config.header.num_fields = new_header->num_fields;
}

static gchar
_header_delimiter(FilterXFunctionEventFormatFormatter *self)
{
  return self->config.header.delimiters[0];
}

static gboolean
_append_signature(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict)
{
  gboolean success = FALSE;

  FilterXObject *version_obj = filterx_object_getattr_string(dict, ctx->config.header.fields[0].name);
  if (!version_obj)
    {
      filterx_eval_push_error_static_info("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "Failed to get version");
      goto exit;
    }

  const gchar *version_str;
  gsize version_len;
  if (!filterx_object_extract_string_ref(version_obj, &version_str, &version_len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "version must be a string, got: %s",
                                          filterx_object_get_type_name(version_obj));
      goto exit;
    }

  g_string_append_printf(formatted, "%s:", ctx->config.signature);

  Field *field = &ctx->config.header.fields[0];
  if (field->field_formatter)
    {
      if (!field->field_formatter(ctx, formatted, dict))
        goto exit;
    }
  else
    {
      g_string_append_len(formatted, version_str, version_len);
      g_string_append_c(formatted, _header_delimiter(ctx->formatter));
    }

  success = TRUE;

exit:
  filterx_object_unref(version_obj);
  return success;
}

gboolean
event_format_formatter_append_header(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict,
                                     const Field *field)
{
  gboolean success = FALSE;

  FilterXObject *value = filterx_object_getattr_string(dict, field->name);
  if (!value)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "Failed to get value for header: %s",
                                          field->name);
      goto exit;
    }

  const gchar *value_str;
  gsize value_len;
  if (!filterx_object_extract_string_ref(value, &value_str, &value_len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "Header value must be a string, got: %s, header: %s",
                                          filterx_object_get_type_name(value), field->name);
      goto exit;
    }

  g_string_append_len(formatted, value_str, value_len);
  for (gsize i = formatted->len - value_len; i < formatted->len; i++)
    {
      if (formatted->str[i] == _header_delimiter(ctx->formatter) || formatted->str[i] == '\\')
        g_string_insert_c(formatted, i++, '\\');
    }
  g_string_append_c(formatted, _header_delimiter(ctx->formatter));

  success = TRUE;

exit:
  filterx_object_unref(value);
  return success;
}

static gboolean
_append_headers(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict)
{
  /*
   * Skipping the first one (version).
   * Skipping the last one (extensions).
   */
  for (gsize i = 1; i < ctx->config.header.num_fields - 1; i++)
    {
      const Field *field = &ctx->config.header.fields[i];

      if (field->field_formatter)
        {
          if (!field->field_formatter(ctx, formatted, dict))
            return FALSE;
          continue;
        }

      if (!event_format_formatter_append_header(ctx, formatted, dict, field))
        return FALSE;
    }

  return TRUE;
}

static gboolean
_append_extension(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  EventFormatterContext *ctx = ((gpointer *) user_data)[0];
  GString *formatted = ((gpointer *) user_data)[1];
  gboolean *first = ((gpointer *) user_data)[2];

  const gchar *key_str;
  gsize key_len;
  if (!filterx_object_extract_string_ref(key, &key_str, &key_len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "Extension key must be a string, got: %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  if (!(*first))
    g_string_append_c(formatted, ctx->config.extensions.pair_separator[0]);

  g_string_append_len(formatted, key_str, key_len);
  g_string_append_c(formatted, ctx->config.extensions.value_separator);

  gsize len_before_value = formatted->len;
  if (!filterx_object_str_append(value, formatted))
    {
      filterx_eval_push_error_static_info("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "Failed to evaluate str() method");
      return FALSE;
    }
  for (gsize i = len_before_value; i < formatted->len; i++)
    {
      if (formatted->str[i] == ctx->config.extensions.value_separator || formatted->str[i] == '\\')
        g_string_insert_c(formatted, i++, '\\');
    }

  *first = FALSE;
  return TRUE;
}

static gboolean
_append_non_separate_extension(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  EventFormatterContext *ctx = ((gpointer *) user_data)[0];
  gsize *fields_to_find = ((gpointer *) user_data)[3];

  const gchar *key_str;
  gsize key_len;
  if (!filterx_object_extract_string_ref(key, &key_str, &key_len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "Extension key must be a string, got: %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  if (*fields_to_find)
    {
      /* -1: don't look for the extensions field */
      for (gsize i = 0; i < ctx->config.header.num_fields - 1; i++)
        {
          const Field *field = &ctx->config.header.fields[i];
          if (strcmp(field->name, key_str) == 0)
            {
              (*fields_to_find)--;
              return TRUE;
            }
        }
    }

  return _append_extension(key, value, user_data);
}

static gboolean
_append_extensions(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict)
{
  gboolean success = FALSE;

  FilterXObject *extensions = filterx_object_getattr_string(dict, "extensions");
  if (!extensions)
    {
      /* -1: non-separate extensions -> extensions field will not be available */
      gsize fields_to_find = ctx->config.header.num_fields - 1;
      gboolean first = TRUE;
      gpointer user_data[] = { ctx, formatted, &first, &fields_to_find };
      success = filterx_object_iter(dict, _append_non_separate_extension, user_data);
      goto exit;
    }

  /* separated extensions */

  FilterXObject *extensions_dict = filterx_ref_unwrap_ro(extensions);
  if (!filterx_object_is_type(extensions_dict, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "extensions must be a dict, got: %s",
                                          filterx_object_get_type_name(extensions));
      goto exit;
    }

  gboolean first = TRUE;
  gpointer user_data[] = { ctx, formatted, &first };
  success = filterx_object_iter(extensions_dict, _append_extension, user_data);

exit:
  filterx_object_unref(extensions);
  return success;
}

static gboolean
_format(FilterXFunctionEventFormatFormatter *self, GString *formatted, FilterXObject *dict)
{
  EventFormatterContext ctx =
  {
    .formatter = self,
    .config = self->config,
  };
  return _append_signature(&ctx, formatted, dict) &&
         _append_headers(&ctx, formatted, dict) &&
         _append_extensions(&ctx, formatted, dict);
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionEventFormatFormatter *self = (FilterXFunctionEventFormatFormatter *) s;

  FilterXObject *result = NULL;

  FilterXObject *msg = filterx_expr_eval_typed(self->msg);
  if (!msg)
    {
      filterx_eval_push_error_static_info("Failed to evaluate event formatter function", s,
                                          "Failed to evaluate msg_dict");
      goto exit;
    }

  FilterXObject *dict = filterx_ref_unwrap_ro(msg);
  if (!filterx_object_is_type(dict, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", s,
                                          "msg_dict must be a dict, got: %s",
                                          filterx_object_get_type_name(msg));
      goto exit;
    }

  ScratchBuffersMarker marker;
  GString *formatted = scratch_buffers_alloc_and_mark(&marker);

  if (_format(self, formatted, dict))
    result = filterx_string_new(formatted->str, formatted->len);

  scratch_buffers_reclaim_marked(marker);

exit:
  filterx_object_unref(msg);
  return result;
}


static void
_free(FilterXExpr *s)
{
  FilterXFunctionEventFormatFormatter *self = (FilterXFunctionEventFormatFormatter *) s;

  filterx_expr_unref(self->msg);
  filterx_function_free_method(&self->super);
}

static gboolean
_extract_arguments(FilterXFunctionEventFormatFormatter *self, FilterXFunctionArgs *args, GError **error)
{
  gsize args_len = filterx_function_args_len(args);
  if (args_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_EVENT_FORMAT_FORMATTER_USAGE,
                  self->super.function_name);
      return FALSE;
    }

  self->msg = filterx_function_args_get_expr(args, 0);

  return TRUE;
}

static gboolean
_event_format_formatter_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionEventFormatFormatter *self = (FilterXFunctionEventFormatFormatter *) s;

  FilterXExpr **exprs[] = { &self->msg };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

gboolean
filterx_function_event_format_formatter_init_instance(FilterXFunctionEventFormatFormatter *self,
                                                      const gchar *fn_name, FilterXFunctionArgs *args,
                                                      Config *config, GError **error)
{
  filterx_function_init_instance(&self->super, fn_name);

  self->super.super.eval = _eval;
  self->super.super.walk_children = _event_format_formatter_walk;
  self->super.super.free_fn = _free;

  g_assert(config->header.num_fields >= 2);

  const gsize first_field_len = strlen(config->header.fields[0].name);
  const gsize version_len = strlen("_version");
  g_assert(first_field_len > version_len); /* Something must be before it. */
  g_assert(strcmp(&config->header.fields[0].name[first_field_len - version_len], "_version") == 0);
  g_assert(strcmp(config->header.fields[config->header.num_fields - 1].name, "extensions") == 0);

  self->config = *config;

  gboolean success = _extract_arguments(self, args, error) && filterx_function_args_check(args, error);

  filterx_function_args_free(args);
  return success;
}

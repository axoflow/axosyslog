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

#include "filterx-func-format-leef.h"
#include "filterx-func-parse-leef.h"
#include "event-format-formatter.h"

#include "filterx/object-extractor.h"

#include "utf8utils.h"

gboolean
filterx_function_format_leef_format_version(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict)
{
  gboolean success = FALSE;

  FilterXObject *version_obj = filterx_object_getattr_string(dict, ctx->config.header.fields[0].name);
  if (!version_obj)
    {
      filterx_eval_push_error_static_info("Failed to evaluate format_leef()", &ctx->formatter->super.super,
                                          "Failed to get version");
      goto exit;
    }

  const gchar *version_str;
  gsize version_len;
  if (!filterx_object_extract_string_ref(version_obj, &version_str, &version_len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate format_leef()", &ctx->formatter->super.super,
                                          "version must be a string, got: %s",
                                          filterx_object_get_type_name(version_obj));
      goto exit;
    }

  if (strcmp(version_str, "2.0") == 0)
    event_format_formatter_context_set_header(ctx, &leef_v2_cfg.header);

  success = event_format_formatter_append_header(ctx, formatted, dict, &ctx->config.header.fields[0]);

exit:
  filterx_object_unref(version_obj);
  return success;
}

gboolean
filterx_function_format_leef_format_delimiter(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict)
{
  gboolean success = FALSE;
  FilterXObject *delimiter_obj = NULL;

  delimiter_obj = filterx_object_getattr_string(dict, "leef_delimiter");
  if (!delimiter_obj)
    {
      g_string_append_c(formatted, '\t');
      goto success;
    }

  const gchar *delimiter_str;
  gsize delimiter_len;
  if (!filterx_object_extract_string_ref(delimiter_obj, &delimiter_str, &delimiter_len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                          "Header value must be a string, got: %s, header: leef_delimiter",
                                          filterx_object_get_type_name(delimiter_obj));
      goto error;
    }

  if (!delimiter_len)
    {
      g_string_append_c(formatted, '\t');
      goto success;
    }

  append_unsafe_utf8_as_escaped(formatted, delimiter_str, delimiter_len, 0, "\\x%02x", "\\x%02x");
  ctx->config.extensions.pair_separator[0] = delimiter_str[0];
  ctx->config.extensions.pair_separator[1] = '\0';

success:
  g_string_append_c(formatted, ctx->formatter->config.header.delimiters[0]);

  success = TRUE;

error:
  filterx_object_unref(delimiter_obj);
  return success;
}

FilterXExpr *
filterx_function_format_leef_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionEventFormatFormatter *self = g_new0(FilterXFunctionEventFormatFormatter, 1);

  if (!filterx_function_event_format_formatter_init_instance(self, "format_leef", args, &leef_v1_cfg, error))
    {
      g_free(self);
      return NULL;
    }

  return &self->super.super;
}

FILTERX_FUNCTION(format_leef, filterx_function_format_leef_new);

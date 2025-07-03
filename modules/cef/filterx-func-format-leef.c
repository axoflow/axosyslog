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
filterx_function_format_leef_format_delimiter(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict)
{
  gboolean success = FALSE;
  FilterXObject *delimiter_obj = NULL;

  FilterXObject *version_obj = filterx_object_getattr_string(dict, "version");
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

  if (strcmp(version_str, "1.0") == 0)
    {
      /* LEEF 1.0 does not have delimiter header */
      success = TRUE;
      goto exit;
    }

  if (strcmp(version_str, "2.0") != 0)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate format_leef()", &ctx->formatter->super.super,
                                          "Unsupported version: %s",
                                          version_str);
      goto exit;
    }

  delimiter_obj = filterx_object_getattr_string(dict, "delimiter");
  if (delimiter_obj)
    {
      const gchar *delimiter_str;
      gsize delimiter_len;
      if (!filterx_object_extract_string_ref(delimiter_obj, &delimiter_str, &delimiter_len))
        {
          filterx_eval_push_error_info_printf("Failed to evaluate event formatter function", &ctx->formatter->super.super,
                                              "Header value for must be a string, got: %s, header: delimiter",
                                              filterx_object_get_type_name(delimiter_obj));
          goto exit;
        }

      if (delimiter_len)
        {
          append_unsafe_utf8_as_escaped(formatted, delimiter_str, delimiter_len, 0, "\\x%02x", "\\x%02x");
          ctx->extension_pair_separator = delimiter_str[0];
        }
    }

  g_string_append_c(formatted, ctx->formatter->config.header.delimiters[0]);

  success = TRUE;

exit:
  filterx_object_unref(version_obj);
  filterx_object_unref(delimiter_obj);
  return success;
}

FilterXExpr *
filterx_function_format_leef_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionEventFormatFormatter *self = g_new0(FilterXFunctionEventFormatFormatter, 1);

  if (!filterx_function_event_format_formatter_init_instance(self, "format_leef", args, &leef_cfg, error))
    {
      g_free(self);
      return NULL;
    }

  return &self->super.super;
}

FILTERX_FUNCTION(format_leef, filterx_function_format_leef_new);

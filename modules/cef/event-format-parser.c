/*
 * Copyright (c) 2024 Axoflow
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

#include <string.h>
#include "event-format-parser.h"
#include "filterx-func-parse-cef.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-generator.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-globals.h"
#include "filterx/object-extractor.h"
#include "filterx/object-json.h"
#include "filterx/object-message-value.h"
#include "filterx/object-null.h"
#include "filterx/filterx-object.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-string.h"

GQuark
event_format_parser_error_quark(void)
{
  return g_quark_from_static_string("event-parser-error-quark");
}

Field
field(FilterXFunctionEventFormatParser *self, int index)
{
  g_assert(index >= 0 && index < self->config.header.num_fields);
  return self->config.header.fields[index];
}

static FilterXObject *
parse_default(FilterXFunctionEventFormatParser *self, const gchar *value, gint value_len, GError **error,
              gpointer user_data)
{
  return filterx_string_new(value, value_len);
}

FilterXObject *
parse_version(FilterXFunctionEventFormatParser *self, const gchar *value, gint value_len, GError **error,
              gpointer user_data)
{
  const gchar *log_signature = self->config.signature;
  gchar *colon_pos = memchr(value, ':', value_len);
  if (!colon_pos || colon_pos == value)
    {
      g_set_error(error, EVENT_FORMAT_PARSER_ERROR, EVENT_FORMAT_PARSER_ERR_NO_LOG_SIGN,
                  EVENT_FORMAT_PARSER_ERR_NO_LOG_SIGN_MSG, log_signature);
      return FALSE;
    }
  gint sign_len = colon_pos - value;
  if (!(strncmp(value, log_signature, sign_len) == 0))
    {
      g_set_error(error, EVENT_FORMAT_PARSER_ERROR, EVENT_FORMAT_PARSER_ERR_LOG_SIGN_DIFFERS,
                  EVENT_FORMAT_PARSER_ERR_LOG_SIGN_DIFFERS_MSG, value, log_signature);
      return FALSE;
    }
  return filterx_string_new(++colon_pos, value_len - sign_len - 1);
}

gboolean
_set_dict_value(FilterXObject *out,
                const gchar *key, gsize key_len,
                const gchar *value, gsize value_len)
{
  FilterXObject *dict_key = filterx_string_new(key, key_len);
  FilterXObject *dict_val = filterx_string_new(value, value_len);

  gboolean ok = filterx_object_set_subscript(out, dict_key, &dict_val);

  filterx_object_unref(dict_key);
  filterx_object_unref(dict_val);
  return ok;
}

static gboolean
_unescape_value_separators(KVScanner *self)
{
  gchar escaped_separator[3] = {'\\', self->value_separator, 0};

  const gchar *start = self->value->str;
  const gchar *pos = start;

  while ((pos = g_strstr_len(start, self->value->len - (pos - start), escaped_separator)) != NULL)
    {
      g_string_append_len(self->decoded_value, start, pos - start);
      g_string_append_c(self->decoded_value, self->value_separator);
      start = pos + 2;
    }
  g_string_append(self->decoded_value, start);
  return TRUE;
}


FilterXObject *
parse_extensions(FilterXFunctionEventFormatParser *self, const gchar *input, gint input_len, GError **error,
                 gpointer user_data)
{
  FilterXObject *fillable = (FilterXObject *)user_data;
  FilterXObject *output = filterx_object_create_dict(fillable);

  KVScanner kv_scanner;
  kv_scanner_init(&kv_scanner, self->config.extensions.value_separator, self->config.extensions.pair_separator, FALSE);
  kv_scanner_set_transform_value(&kv_scanner, _unescape_value_separators);
  kv_scanner_input(&kv_scanner, input);
  while (kv_scanner_scan_next(&kv_scanner))
    {
      const gchar *name = kv_scanner_get_current_key(&kv_scanner);
      gsize name_len = kv_scanner_get_current_key_len(&kv_scanner);
      const gchar *value = kv_scanner_get_current_value(&kv_scanner);
      gsize value_len = kv_scanner_get_current_value_len(&kv_scanner);

      if (!_set_dict_value(output, name, name_len, value, value_len))
        goto exit;
    }

exit:
  kv_scanner_deinit(&kv_scanner);
  return output;
}

static inline gboolean
_fill_object_col(FilterXFunctionEventFormatParser *self, gint64 index, const gchar *input, gint input_len,
                 FilterXObject *fillable,
                 GError **error)
{
  Field f = field(self, index);
  FilterXObject *key = filterx_string_new(f.name, -1);
  FilterXObject *val = NULL;

  if (!f.field_parser)
    val = parse_default(self, input, input_len, error, fillable);
  else
    val = f.field_parser(self, input, input_len, error, fillable);

  gboolean ok = FALSE;
  if (!*error)
    ok = filterx_object_set_subscript(fillable, key, &val);

  filterx_object_unref(val);
  filterx_object_unref(key);
  return ok;
}

static gboolean
parse(FilterXFunctionEventFormatParser *self, const gchar *log, gsize len, FilterXObject *fillable, GError **error)
{
  gboolean ok = FALSE;
  gsize num_fields = self->config.header.num_fields;

  CSVScanner csv_scanner;
  csv_scanner_init(&csv_scanner, &self->csv_opts, log);

  guint64 i = 0;
  while (csv_scanner_scan_next(&csv_scanner))
    {
      if (i >= num_fields)
        break;

      const gchar *input = csv_scanner_get_current_value(&csv_scanner);
      gint input_len = csv_scanner_get_current_value_len(&csv_scanner);

      ok = _fill_object_col(self, i, input, input_len, fillable, error);
      if(!ok || *error)
        goto exit;

      i++;
    }

  if (i < self->csv_opts.expected_columns)
    {
      g_set_error(error, EVENT_FORMAT_PARSER_ERROR, EVENT_FORMAT_PARSER_ERR_MISSING_COLUMNS,
                  EVENT_FORMAT_PARSER_ERR_MISSING_COLUMNS_MSG, i, self->config.header.num_fields);
    }


exit:
  csv_scanner_deinit(&csv_scanner);

  return ok;
}

static gboolean
_generate(FilterXExprGenerator *s, FilterXObject *fillable)
{
  FilterXFunctionEventFormatParser *self = (FilterXFunctionEventFormatParser *) s;
  gboolean ok = FALSE;

  FilterXObject *obj = filterx_expr_eval(self->msg);
  if (!obj)
    return FALSE;

  gsize len;
  const gchar *input;
  if (!filterx_object_extract_string_ref(obj, &input, &len))
    {
      filterx_eval_push_error_info(EVENT_FORMAT_PARSER_ERR_NOT_STRING_INPUT_MSG, &self->super.super.super,
                                   NULL, TRUE);
      ok = FALSE;
      goto exit;
    }

  GError *error = NULL;

  ok = parse(self, input, len, fillable, &error);

  if (error)
    {
      filterx_eval_push_error_info(g_strdup(error->message), &self->super.super.super,
                                   NULL, TRUE);
      ok = FALSE;
      g_error_free(error);
    }

exit:
  filterx_object_unref(obj);
  return ok;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionEventFormatParser *self = (FilterXFunctionEventFormatParser *) s;
  filterx_expr_unref(self->msg);
  csv_scanner_options_clean(&self->csv_opts);
  filterx_generator_function_free_method(&self->super);

}

static FilterXExpr *
_extract_msg_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *msg_expr = filterx_function_args_get_expr(args, 0);
  if (!msg_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: msg_str. ");
      return NULL;
    }

  return msg_expr;
}

static gboolean
_extract_args(FilterXFunctionEventFormatParser *self, FilterXFunctionArgs *args, GError **error)
{
  gsize args_len = filterx_function_args_len(args);
  if (args_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. ");
      return FALSE;
    }

  self->msg = _extract_msg_expr(args, error);
  if (!self->msg)
    return FALSE;

  return TRUE;
}

static void
_set_config(FilterXFunctionEventFormatParser *self, Config *cfg)
{
  g_assert(cfg);
  self->config = *cfg;
  csv_scanner_options_set_delimiters(&self->csv_opts, cfg->header.delimiters);
  csv_scanner_options_set_quote_pairs(&self->csv_opts, "");
  csv_scanner_options_set_dialect(&self->csv_opts, CSV_SCANNER_ESCAPE_UNQUOTED_DELIMITER);
  csv_scanner_options_set_expected_columns(&self->csv_opts, cfg->header.num_fields);
  self->csv_opts.flags |= CSV_SCANNER_GREEDY;
}

gboolean
filterx_function_parser_init_instance(FilterXFunctionEventFormatParser *self, const gchar *fn_name,
                                      FilterXFunctionArgs *args, Config *cfg, GError **error)
{
  filterx_generator_function_init_instance(&self->super, fn_name);
  self->super.super.generate = _generate;
  self->super.super.create_container = filterx_generator_create_dict_container;
  self->super.super.super.free_fn = _free;

  _set_config(self, cfg);

  if (!_extract_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    return FALSE;
  return TRUE;
}

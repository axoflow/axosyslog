/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
 * Copyright (c) 2024 shifter
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

#include <string.h>
#include "event-format-parser.h"
#include "filterx-func-parse-cef.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-globals.h"
#include "filterx/object-extractor.h"
#include "filterx/object-message-value.h"
#include "filterx/object-null.h"
#include "filterx/filterx-object.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"

GQuark
event_format_parser_error_quark(void)
{
  return g_quark_from_static_string("event-parser-error-quark");
}

static Field *
_get_current_field(EventParserContext *ctx)
{
  g_assert(ctx->field_index >= 0 && ctx->field_index < ctx->config.header.num_fields);
  return &ctx->config.header.fields[ctx->field_index];
}

void
event_format_parser_context_set_header(EventParserContext *ctx, Header *new_header)
{
  ctx->config.header.fields = new_header->fields;
  ctx->config.header.num_fields = new_header->num_fields;
  csv_scanner_set_expected_columns(ctx->csv_scanner, new_header->num_fields);
}

static FilterXObject *
_create_unescaped_string_obj(const gchar *value, gint value_len, const gchar *chars_to_unescape)
{
  GString *unescaped = g_string_new_len(NULL, value_len);

  for (gint i = 0; i < value_len; i++)
    {
      if (value[i] == '\\' && i + 1 < value_len && strchr(chars_to_unescape, value[i + 1]))
        {
          g_string_append_c(unescaped, value[i + 1]);
          i++;
          continue;
        }

      g_string_append_c(unescaped, value[i]);
    }

  gssize unascaped_len = unescaped->len;
  gchar *unescaped_cstr = g_string_free(unescaped, FALSE);
  return filterx_string_new_take(unescaped_cstr, unascaped_len);
}

static gboolean
parse_default(EventParserContext *ctx, const gchar *value, gint value_len, FilterXObject *parsed_dict)
{
  if ((!value || value_len <= 0) && !csv_scanner_has_input_left(ctx->csv_scanner))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event format parser", &ctx->parser->super.super,
                                          "Header '%s' is missing",
                                          _get_current_field(ctx)->name);
      return FALSE;
    }

  FILTERX_STRING_DECLARE_ON_STACK(key_obj, _get_current_field(ctx)->name, -1);
  FilterXObject *value_obj = _create_unescaped_string_obj(value, value_len, "\\");

  filterx_object_set_subscript(parsed_dict, key_obj, &value_obj);

  filterx_object_unref(value_obj);
  filterx_object_unref(key_obj);
  return TRUE;
}

gboolean
event_format_parser_parse_version(EventParserContext *ctx, const gchar *value, gint value_len,
                                  FilterXObject *parsed_dict)
{
  const gchar *log_signature = ctx->parser->config.signature;
  gchar *colon_pos = memchr(value, ':', value_len);
  if (!colon_pos || colon_pos == value)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event format parser", &ctx->parser->super.super,
                                          EVENT_FORMAT_PARSER_ERR_NO_LOG_SIGN_MSG,
                                          log_signature);
      return FALSE;
    }

  gint sign_len = colon_pos - value;
  if (!(strncmp(value, log_signature, sign_len) == 0))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate event format parser", &ctx->parser->super.super,
                                          EVENT_FORMAT_PARSER_ERR_LOG_SIGN_DIFFERS_MSG,
                                          value, log_signature);
      return FALSE;
    }

  FILTERX_STRING_DECLARE_ON_STACK(key_obj, "version", 7);
  FilterXObject *value_obj = filterx_string_new(++colon_pos, value_len - sign_len - 1);

  filterx_object_set_subscript(parsed_dict, key_obj, &value_obj);

  filterx_object_unref(value_obj);
  filterx_object_unref(key_obj);
  return TRUE;
}

static gboolean
_set_dict_value(EventParserContext *ctx, FilterXObject *out,
                const gchar *key, gsize key_len,
                const gchar *value, gsize value_len)
{
  const gchar chars_to_unescape[] = { '\\', ctx->config.extensions.value_separator, '\0' };

  FILTERX_STRING_DECLARE_ON_STACK(dict_key, key, key_len);
  FilterXObject *dict_val = _create_unescaped_string_obj(value, value_len, chars_to_unescape);

  gboolean ok = filterx_object_set_subscript(out, dict_key, &dict_val);

  filterx_object_unref(dict_key);
  filterx_object_unref(dict_val);
  return ok;
}

gboolean
event_format_parser_parse_extensions(EventParserContext *ctx, const gchar *input, gint input_len,
                                     FilterXObject *parsed_dict)
{
  FilterXObject *dict_to_fill = parsed_dict;

  if (ctx->separate_extensions)
    {
      dict_to_fill = filterx_object_create_dict(parsed_dict);
      FILTERX_STRING_DECLARE_ON_STACK(key, "extensions", 10);
      filterx_object_set_subscript(parsed_dict, key, &dict_to_fill);
      filterx_object_unref(key);
    }

  gboolean success = FALSE;

  KVScanner kv_scanner;
  kv_scanner_init(&kv_scanner, ctx->config.extensions.value_separator, ctx->config.extensions.pair_separator,
                  KVSSWM_APPEND_TO_LAST_VALUE);
  kv_scanner_input(&kv_scanner, input);
  while (kv_scanner_scan_next(&kv_scanner))
    {
      const gchar *name = kv_scanner_get_current_key(&kv_scanner);
      gsize name_len = kv_scanner_get_current_key_len(&kv_scanner);
      const gchar *value = kv_scanner_get_current_value(&kv_scanner);
      gsize value_len = kv_scanner_get_current_value_len(&kv_scanner);

      if (!_set_dict_value(ctx, dict_to_fill, name, name_len, value, value_len))
        goto exit;
    }

  success = TRUE;

exit:
  kv_scanner_deinit(&kv_scanner);
  return success;
}

static gboolean
_parse_column(EventParserContext *ctx, FilterXObject *parsed_dict)
{
  const gchar *input = csv_scanner_get_current_value(ctx->csv_scanner);
  gint input_len = csv_scanner_get_current_value_len(ctx->csv_scanner);
  Field *field = _get_current_field(ctx);

  if (!field->field_parser)
    return parse_default(ctx, input, input_len, parsed_dict);
  return field->field_parser(ctx, input, input_len, parsed_dict);
}

static EventParserContext
_new_context(FilterXFunctionEventFormatParser *self,  CSVScanner *csv_scanner)
{
  EventParserContext ctx =
  {
    .parser = self,
    .config = self->config,
    .field_index = 0,
    .csv_scanner = csv_scanner,
    .separate_extensions = self->separate_extensions,
  };

  if (self->kv_value_separator != 0)
    ctx.config.extensions.value_separator = self->kv_value_separator;

  if (self->kv_pair_separator)
    g_strlcpy(ctx.config.extensions.pair_separator, self->kv_pair_separator,
              EVENT_FORMAT_PARSER_PAIR_SEPARATOR_MAX_LEN);

  return ctx;
}

static FilterXObject *
_parse(FilterXFunctionEventFormatParser *self, const gchar *log, gsize len)
{
  FilterXObject *result = filterx_dict_new();

  CSVScanner csv_scanner;
  csv_scanner_init(&csv_scanner, &self->csv_opts, log);
  csv_scanner_set_expected_columns(&csv_scanner, self->config.header.num_fields);
  EventParserContext ctx = _new_context(self, &csv_scanner);

  while (ctx.field_index < ctx.config.header.num_fields && !csv_scanner_is_scan_complete(&csv_scanner))
    {
      csv_scanner_scan_next(&csv_scanner);

      if (!_parse_column(&ctx, result))
        {
          filterx_object_unref(result);
          result = NULL;
          break;
        }

      ctx.field_index++;
    }

  csv_scanner_deinit(&csv_scanner);
  return result;
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionEventFormatParser *self = (FilterXFunctionEventFormatParser *) s;

  FilterXObject *result = NULL;

  FilterXObject *obj = filterx_expr_eval(self->msg);
  if (!obj)
    {
      filterx_eval_push_error_static_info("Failed to evaluate event format parser", &self->super.super,
                                          "Failed to evaluate input expression");
      return NULL;
    }

  gsize len;
  const gchar *input;
  if (!filterx_object_extract_string_ref(obj, &input, &len))
    {
      filterx_eval_push_error_static_info("Failed to evaluate event format parser", &self->super.super,
                                          EVENT_FORMAT_PARSER_ERR_NOT_STRING_INPUT_MSG);
      goto exit;
    }

  result = _parse(self, input, len);

exit:
  filterx_object_unref(obj);
  return result;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionEventFormatParser *self = (FilterXFunctionEventFormatParser *) s;

  self->msg = filterx_expr_optimize(self->msg);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionEventFormatParser *self = (FilterXFunctionEventFormatParser *) s;

  if (!filterx_expr_init(self->msg, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionEventFormatParser *self = (FilterXFunctionEventFormatParser *) s;
  filterx_expr_deinit(self->msg, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionEventFormatParser *self = (FilterXFunctionEventFormatParser *) s;
  filterx_expr_unref(self->msg);
  g_free(self->kv_pair_separator);
  csv_scanner_options_clean(&self->csv_opts);
  filterx_function_free_method(&self->super);
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
_extract_optional_args(FilterXFunctionEventFormatParser *self, FilterXFunctionArgs *args, GError **error)
{
  gboolean exists;
  gsize len;
  const gchar *value;
  gboolean bool_value;
  gboolean arg_error;

  value = filterx_function_args_get_named_literal_string(args, EVENT_FORMAT_PARSER_ARG_NAME_PAIR_SEPARATOR, &len,
                                                         &exists);
  if (exists)
    {
      if (len < 1 || !value)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      EVENT_FORMAT_PARSER_ERR_EMPTY_STRING, EVENT_FORMAT_PARSER_ARG_NAME_PAIR_SEPARATOR);
          goto error;
        }
      if (len > EVENT_FORMAT_PARSER_PAIR_SEPARATOR_MAX_LEN - 1)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      EVENT_FORMAT_PARSER_ERR_SEPARATOR_MAX_LENGTH_EXCEEDED, EVENT_FORMAT_PARSER_ARG_NAME_PAIR_SEPARATOR);
          goto error;
        }
      self->kv_pair_separator = g_strdup(value);
    }
  value = filterx_function_args_get_named_literal_string(args, EVENT_FORMAT_PARSER_ARG_NAME_VALUE_SEPARATOR, &len,
                                                         &exists);
  if (exists)
    {
      if (len < 1 || !value)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      EVENT_FORMAT_PARSER_ERR_EMPTY_STRING, EVENT_FORMAT_PARSER_ARG_NAME_VALUE_SEPARATOR);
          goto error;
        }
      self->kv_value_separator = value[0];
    }
  bool_value = filterx_function_args_get_named_literal_boolean(args, EVENT_FORMAT_PARSER_ARG_SEPARATE_EXTENSIONS,
               &exists, &arg_error);
  if (exists)
    {
      if (arg_error)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      EVENT_FORMAT_PARSER_ERR_MUST_BE_BOOLEAN, EVENT_FORMAT_PARSER_ARG_SEPARATE_EXTENSIONS);
          goto error;
        }
      self->separate_extensions = bool_value;
    }
  return TRUE;
error:
  return FALSE;
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

  if (!_extract_optional_args(self, args, error))
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
  csv_scanner_options_set_flags(&self->csv_opts, CSV_SCANNER_GREEDY);
}

gboolean
filterx_function_parser_init_instance(FilterXFunctionEventFormatParser *self, const gchar *fn_name,
                                      FilterXFunctionArgs *args, Config *cfg, GError **error)
{
  filterx_function_init_instance(&self->super, fn_name);
  self->super.super.eval = _eval;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.free_fn = _free;

  _set_config(self, cfg);

  if (!_extract_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    return FALSE;
  return TRUE;
}

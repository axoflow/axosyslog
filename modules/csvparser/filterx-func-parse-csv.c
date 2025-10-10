/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter
 *
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

#include "filterx-func-parse-csv.h"
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
#include "filterx/object-dict.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-list.h"
#include "filterx/filterx-sequence.h"

#include "scanner/csv-scanner/csv-scanner.h"
#include "parser/parser-expr.h"
#include "scratch-buffers.h"
#include "str-utils.h"
#include "csvparser.h"

typedef struct FilterXFunctionParseCSV_
{
  FilterXFunction super;
  FilterXExpr *msg;
  CSVScannerOptions options;
  FilterXExpr *string_delimiters;
  FilterXExpr *columns;
} FilterXFunctionParseCSV;

static inline gboolean
_are_column_names_set(FilterXFunctionParseCSV *self)
{
  return !!self->columns;
}

static gboolean
_parse_list_argument(FilterXFunctionParseCSV *self, FilterXExpr *list_expr, GList **list, const gchar *arg_name)
{
  gboolean result = FALSE;
  if (!list_expr)
    return TRUE;
  FilterXObject *obj = filterx_expr_eval(list_expr);
  if (!obj)
    return FALSE;

  FilterXObject *list_obj = filterx_ref_unwrap_ro(obj);
  if (!filterx_object_is_type(list_obj, &FILTERX_TYPE_NAME(sequence)))
    {
      filterx_eval_push_error_info_printf("Failed to initialize parse_csv()", &self->super.super,
                                          "Argument %s must be a list, got: %s",
                                          arg_name, filterx_object_get_type_name(obj));
      goto exit;
    }

  guint64 size;
  if (!filterx_object_len(list_obj, &size))
    {
      filterx_eval_push_error_static_info("Failed to initialize parse_csv()",
                                          &self->super.super, "len() method failed");
      return FALSE;
    }

  for (guint64 i = 0; i < size; i++)
    {
      FilterXObject *elt = filterx_sequence_get_subscript(list_obj, i);

      const gchar *val;
      gsize len;
      if (filterx_object_extract_string_ref(elt, &val, &len))
        *list = g_list_append(*list, g_strndup(val, len));
      filterx_object_unref(elt);
    }

  result = TRUE;
exit:
  filterx_object_unref(obj);
  return result;
}

static inline void
_init_scanner(FilterXFunctionParseCSV *self, GList *string_delimiters, gint num_of_cols, const gchar *input,
              CSVScanner *scanner, CSVScannerOptions *local_opts)
{
  CSVScannerOptions *opts = &self->options;

  if (string_delimiters)
    {
      csv_scanner_options_copy(local_opts, &self->options);
      opts = local_opts;
      csv_scanner_options_set_string_delimiters(local_opts, string_delimiters);
    }

  csv_scanner_init(scanner, opts, input);
  csv_scanner_set_expected_columns(scanner, num_of_cols);
}

static inline gboolean
_maybe_init_columns(FilterXFunctionParseCSV *self, FilterXObject **columns, guint64 *num_of_columns)
{
  if (!self->columns)
    {
      *columns = NULL;
      *num_of_columns = 0;
      return TRUE;
    }

  *columns = filterx_expr_eval(self->columns);
  if (!*columns)
    {
      filterx_eval_push_error_static_info("Failed to initialize parse_csv()", &self->super.super,
                                          "Failed to evaluate " FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS " argument");
      return FALSE;
    }

  FilterXObject *cols_unwrapped = filterx_ref_unwrap_ro(*columns);
  if (!filterx_object_is_type(cols_unwrapped, &FILTERX_TYPE_NAME(sequence)))
    {
      filterx_eval_push_error_info_printf("Failed to initialize parse_csv()", &self->super.super,
                                          "Argument " FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS " must be a list, got: %s",
                                          filterx_object_get_type_name(*columns));
      return FALSE;
    }

  if (!filterx_object_len(cols_unwrapped, num_of_columns))
    {
      filterx_eval_push_error_static_info("Failed to initialize parse_csv()", &self->super.super,
                                          "len() method failed on " FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS " argument");
      return FALSE;
    }

  return TRUE;
}

static inline gboolean
_fill_object_col(FilterXFunctionParseCSV *self, FilterXObject *cols, gint64 index, CSVScanner *scanner,
                 FilterXObject *result)
{
  FilterXObject *col;
  col = filterx_sequence_get_subscript(cols, index);

  FILTERX_STRING_DECLARE_ON_STACK(val,
                                  csv_scanner_get_current_value(scanner),
                                  csv_scanner_get_current_value_len(scanner));

  gboolean ok = filterx_object_set_subscript(result, col, &val);
  if (!ok)
    {
      filterx_eval_push_error_static_info("Failed to evaluate parse_csv()", &self->super.super,
                                          "set-subscript() method failed");
    }

  FILTERX_STRING_CLEAR_FROM_STACK(val);
  filterx_object_unref(col);

  return ok;
}

static inline gboolean
_fill_array_element(CSVScanner *scanner, FilterXObject *result)
{
  FILTERX_STRING_DECLARE_ON_STACK(val,
                                  csv_scanner_get_current_value(scanner),
                                  csv_scanner_get_current_value_len(scanner));

  gboolean ok = filterx_sequence_append(result, &val);
  if (!ok)
    {
      filterx_eval_push_error_static_info("Failed to evaluate parse_csv()", NULL,
                                          "append() method failed");
    }

  FILTERX_STRING_CLEAR_FROM_STACK(val);

  return ok;
}

static FilterXObject *
_eval_parse_csv(FilterXExpr *s)
{
  FilterXFunctionParseCSV *self = (FilterXFunctionParseCSV *) s;
  FilterXObject *result = NULL;

  GList *string_delimiters = NULL;
  guint64 num_of_columns = 0;
  FilterXObject *cols = NULL;

  FilterXObject *obj = filterx_expr_eval(self->msg);
  if (!obj)
    {
      filterx_eval_push_error_static_info("Failed to evaluate parse_csv()", &self->super.super,
                                          "Failed to evaluate expression");
      goto exit;
    }

  gsize len;
  const gchar *input;
  if (!filterx_object_extract_string_ref(obj, &input, &len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate parse_csv()", &self->super.super,
                                          "Input must be a string, got: %s. " FILTERX_FUNC_PARSE_CSV_USAGE,
                                          filterx_object_get_type_name(obj));
      goto exit;
    }

  APPEND_ZERO(input, input, len);

  if (!_parse_list_argument(self, self->string_delimiters, &string_delimiters,
                            FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS))
    goto exit;

  if (!_maybe_init_columns(self, &cols, &num_of_columns))
    goto exit;

  if (_are_column_names_set(self))
    result = filterx_dict_sized_new(num_of_columns);
  else
    result = filterx_list_new();

  CSVScannerOptions local_opts = {0};
  CSVScanner scanner;
  _init_scanner(self, string_delimiters, num_of_columns, input, &scanner, &local_opts);

  guint64 i = 0;
  gboolean ok = TRUE;
  while (csv_scanner_scan_next(&scanner) && ok)
    {
      if (_are_column_names_set(self))
        {
          if (i >= num_of_columns)
            break;

          ok = _fill_object_col(self, cols, i, &scanner, result);

          i++;
        }
      else
        {
          ok = _fill_array_element(&scanner, result);
        }
    }
  if (!ok)
    {
      filterx_object_unref(result);
      result = NULL;
    }
exit:
  csv_scanner_options_clean(&local_opts);
  filterx_object_unref(cols);
  filterx_object_unref(obj);
  csv_scanner_deinit(&scanner);
  return result;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionParseCSV *self = (FilterXFunctionParseCSV *) s;

  self->msg = filterx_expr_optimize(self->msg);
  self->columns = filterx_expr_optimize(self->columns);
  self->string_delimiters = filterx_expr_optimize(self->string_delimiters);

  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionParseCSV *self = (FilterXFunctionParseCSV *) s;

  if (!filterx_expr_init(self->msg, cfg))
    return FALSE;

  if (!filterx_expr_init(self->columns, cfg))
    {
      filterx_expr_deinit(self->msg, cfg);
      return FALSE;
    }

  if (!filterx_expr_init(self->string_delimiters, cfg))
    {
      filterx_expr_deinit(self->msg, cfg);
      filterx_expr_deinit(self->columns, cfg);
      return FALSE;
    }

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionParseCSV *self = (FilterXFunctionParseCSV *) s;
  filterx_expr_deinit(self->msg, cfg);
  filterx_expr_deinit(self->columns, cfg);
  filterx_expr_deinit(self->string_delimiters, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionParseCSV *self = (FilterXFunctionParseCSV *) s;
  filterx_expr_unref(self->msg);
  filterx_expr_unref(self->columns);
  filterx_expr_unref(self->string_delimiters);
  csv_scanner_options_clean(&self->options);
  filterx_function_free_method(&self->super);
}

static FilterXExpr *
_extract_msg_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *msg_expr = filterx_function_args_get_expr(args, 0);
  if (!msg_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: msg_str. " FILTERX_FUNC_PARSE_CSV_USAGE);
      return NULL;
    }

  return msg_expr;
}

static FilterXExpr *
_extract_columns_expr(FilterXFunctionArgs *args, GError **error)
{
  return filterx_function_args_get_named_expr(args, FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS);
}

static FilterXExpr *
_extract_stringdelimiters_expr(FilterXFunctionArgs *args, GError **error)
{
  return filterx_function_args_get_named_expr(args, FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS);
}

static gboolean
_set_quote_pair(gsize i, FilterXExpr *qpair, gpointer user_data)
{
  GString *quotes = ((gpointer *) user_data)[0];
  const gchar **error_str = ((gpointer *) user_data)[1];

  if (!filterx_expr_is_literal(qpair))
    goto error;

  FilterXObject *quote_pair = filterx_literal_get_value(qpair);

  const gchar *pair;
  gsize len;
  if (!filterx_object_extract_string_ref(quote_pair, &pair, &len))
    {
      filterx_object_unref(quote_pair);
      goto error;
    }

  if (len != 1 && len != 2)
    {
      filterx_object_unref(quote_pair);
      *error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_QUOTE_PAIRS" elements must be 1 or 2 characters long";
      return FALSE;
    }

  if (len == 1)
    {
      g_string_append_c(quotes, pair[0]);
      g_string_append_c(quotes, pair[0]);
    }
  else
    g_string_append_len(quotes, pair, len);

  filterx_object_unref(quote_pair);

  return TRUE;

error:
  *error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_QUOTE_PAIRS" must be a literal string array";
  return FALSE;
}

static gboolean
_extract_opts(FilterXFunctionParseCSV *self, FilterXFunctionArgs *args, GError **error)
{
  guint32 opt_flags = self->options.flags;

  const gchar *error_str = "";
  gboolean exists;
  gsize len;
  const gchar *value;
  gboolean flag_err = FALSE;
  gboolean flag_val = FALSE;

  value = filterx_function_args_get_named_literal_string(args, FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER, &len, &exists);
  if (exists)
    {
      if (len < 1 && !self->string_delimiters)
        {
          error_str = FILTERX_FUNC_PARSE_ERR_EMPTY_DELIMITER;
          goto error;
        }
      if (!value)
        {
          error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER " must be a string literal";
          goto error;
        }
      csv_scanner_options_set_delimiters(&self->options, value);
    }

  value = filterx_function_args_get_named_literal_string(args, FILTERX_FUNC_PARSE_CSV_ARG_NAME_DIALECT, &len, &exists);
  if (exists)
    {
      if (len < 1)
        {
          error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_DIALECT " can not be empty";
          goto error;
        }
      if (!value)
        {
          error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_DIALECT " must be a string literal";
          goto error;
        }
      CSVScannerDialect dialect = csv_parser_lookup_dialect(value);
      if (dialect == -1)
        {
          error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_DIALECT " argument must be one of: [" \
                      "escape-none, " \
                      "escape-backslash, " \
                      "escape-backslash-with-sequences, " \
                      "escape-double-char]";
          goto error;
        }
      csv_scanner_options_set_dialect(&self->options, dialect);
    }

  flag_val = filterx_function_args_get_named_literal_boolean(args, FILTERX_FUNC_PARSE_CSV_ARG_NAME_GREEDY, &exists,
                                                             &flag_err);
  if (exists)
    {

      if (flag_err)
        {
          error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_GREEDY " argument evaluation error";
          goto error;
        }

      if (flag_val)
        opt_flags |= CSV_SCANNER_GREEDY;
      else
        opt_flags &= ~CSV_SCANNER_GREEDY;
    }

  flag_val = filterx_function_args_get_named_literal_boolean(args, FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRIP_WHITESPACE,
                                                             &exists,
                                                             &flag_err);
  if (!exists)
    flag_val = filterx_function_args_get_named_literal_boolean(args, FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRIP_WHITESPACES,
                                                               &exists,
                                                               &flag_err);
  if (exists)
    {

      if (flag_err)
        {
          error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRIP_WHITESPACE " argument evaluation error";
          goto error;
        }

      if (flag_val)
        opt_flags |= CSV_SCANNER_STRIP_WHITESPACE;
      else
        opt_flags &= ~CSV_SCANNER_STRIP_WHITESPACE;
    }

  csv_scanner_options_set_flags(&self->options, opt_flags);

  FilterXExpr *quote_pairs = filterx_function_args_get_named_expr(args, FILTERX_FUNC_PARSE_CSV_ARG_NAME_QUOTE_PAIRS);
  if (quote_pairs)
    {
      if (!filterx_expr_is_literal_list(quote_pairs))
        {
          error_str = FILTERX_FUNC_PARSE_CSV_ARG_NAME_QUOTE_PAIRS" must be a literal string array";
          goto error;
        }

      ScratchBuffersMarker m;
      GString *quotes = scratch_buffers_alloc_and_mark(&m);
      gpointer user_data[] = { quotes, &error_str };
      if (!filterx_literal_list_foreach(quote_pairs, _set_quote_pair, user_data))
        {
          scratch_buffers_reclaim_marked(m);
          filterx_expr_unref(quote_pairs);
          goto error;
        }

      csv_scanner_options_set_quote_pairs(&self->options, quotes->str);
      scratch_buffers_reclaim_marked(m);
      filterx_expr_unref(quote_pairs);
    }

  return TRUE;
error:
  g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
              "%s. %s", error_str, FILTERX_FUNC_PARSE_CSV_USAGE);
  return FALSE;
}

static gboolean
_extract_args(FilterXFunctionParseCSV *self, FilterXFunctionArgs *args, GError **error)
{
  gsize args_len = filterx_function_args_len(args);
  if (args_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_PARSE_CSV_USAGE);
      return FALSE;
    }

  self->msg = _extract_msg_expr(args, error);
  if (!self->msg)
    return FALSE;

  self->columns = _extract_columns_expr(args, error);
  self->string_delimiters = _extract_stringdelimiters_expr(args, error);

  if (!_extract_opts(self, args, error))
    return FALSE;

  return TRUE;
}

FilterXExpr *
filterx_function_parse_csv_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionParseCSV *self = g_new0(FilterXFunctionParseCSV, 1);
  filterx_function_init_instance(&self->super, "parse_csv");
  self->super.super.eval = _eval_parse_csv;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.free_fn = _free;
  csv_scanner_options_set_delimiters(&self->options, ",");
  csv_scanner_options_set_quote_pairs(&self->options, "\"\"''");
  csv_scanner_options_set_dialect(&self->options, CSV_SCANNER_ESCAPE_NONE);

  if (!_extract_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(parse_csv, filterx_function_parse_csv_new);

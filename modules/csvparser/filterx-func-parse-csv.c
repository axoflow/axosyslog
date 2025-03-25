/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter
 *
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

#include "filterx-func-parse-csv.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-generator.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-globals.h"
#include "filterx/object-extractor.h"
#include "filterx/object-message-value.h"
#include "filterx/object-null.h"
#include "filterx/filterx-object.h"
#include "filterx/object-dict.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list.h"
#include "filterx/object-list-interface.h"

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

  struct
  {
    FilterXExpr *expr;
    GPtrArray *literals;
  } columns;
} FilterXFunctionParseCSV;

static inline gboolean
_are_column_names_set(FilterXFunctionParseCSV *self)
{
  return self->columns.expr || self->columns.literals;
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
  if (!filterx_object_is_type(list_obj, &FILTERX_TYPE_NAME(list)))
    {
      msg_error("list object argument must be a type of list.",
                evt_tag_str("current_type", list_obj->type->name ),
                evt_tag_str("argument_name", arg_name));
      goto exit;
    }

  guint64 size;
  if (!filterx_object_len(list_obj, &size))
    return FALSE;

  for (guint64 i = 0; i < size; i++)
    {
      FilterXObject *elt = filterx_list_get_subscript(list_obj, i);

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
  if (self->columns.literals)
    {
      *num_of_columns = self->columns.literals->len;
      *columns = NULL;
      return TRUE;
    }

  if (!self->columns.expr)
    {
      *columns = NULL;
      *num_of_columns = 0;
      return TRUE;
    }

  *columns = filterx_expr_eval(self->columns.expr);
  if (!*columns)
    return FALSE;

  FilterXObject *cols_unwrapped = filterx_ref_unwrap_ro(*columns);
  if (!filterx_object_is_type(cols_unwrapped, &FILTERX_TYPE_NAME(list)))
    {
      msg_error("list object argument must be a type of list.",
                evt_tag_str("current_type", cols_unwrapped->type->name),
                evt_tag_str("argument_name", FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS));
      return FALSE;
    }

  if (!filterx_object_len(cols_unwrapped, num_of_columns))
    return FALSE;

  return TRUE;
}

static inline gboolean
_fill_object_col(FilterXFunctionParseCSV *self, FilterXObject *cols, gint64 index, CSVScanner *scanner,
                 FilterXObject *result)
{
  FilterXObject *col;
  if (self->columns.literals)
    col = filterx_object_ref(g_ptr_array_index(self->columns.literals, index));
  else
    col = filterx_list_get_subscript(cols, index);

  FILTERX_STRING_DECLARE_ON_STACK(val,
                                  csv_scanner_get_current_value(scanner),
                                  csv_scanner_get_current_value_len(scanner));

  gboolean ok = filterx_object_set_subscript(result, col, &val);

  filterx_object_unref(val);
  filterx_object_unref(col);

  return ok;
}

static inline gboolean
_fill_array_element(CSVScanner *scanner, FilterXObject *result)
{
  FILTERX_STRING_DECLARE_ON_STACK(val,
                                  csv_scanner_get_current_value(scanner),
                                  csv_scanner_get_current_value_len(scanner));

  gboolean ok = filterx_list_append(result, &val);

  filterx_object_unref(val);

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
    goto exit;

  gsize len;
  const gchar *input;
  if (!filterx_object_extract_string_ref(obj, &input, &len))
    goto exit;

  APPEND_ZERO(input, input, len);

  if (!_parse_list_argument(self, self->string_delimiters, &string_delimiters,
                            FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS))
    goto exit;

  if(!_maybe_init_columns(self, &cols, &num_of_columns))
    goto exit;

  if (_are_column_names_set(self))
    result = filterx_dict_new();
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
  self->columns.expr = filterx_expr_optimize(self->columns.expr);
  self->string_delimiters = filterx_expr_optimize(self->string_delimiters);

  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionParseCSV *self = (FilterXFunctionParseCSV *) s;

  if (!filterx_expr_init(self->msg, cfg))
    return FALSE;

  if (!filterx_expr_init(self->columns.expr, cfg))
    {
      filterx_expr_deinit(self->msg, cfg);
      return FALSE;
    }

  if (!filterx_expr_init(self->string_delimiters, cfg))
    {
      filterx_expr_deinit(self->msg, cfg);
      filterx_expr_deinit(self->columns.expr, cfg);
      return FALSE;
    }

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionParseCSV *self = (FilterXFunctionParseCSV *) s;
  filterx_expr_deinit(self->msg, cfg);
  filterx_expr_deinit(self->columns.expr, cfg);
  filterx_expr_deinit(self->string_delimiters, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionParseCSV *self = (FilterXFunctionParseCSV *) s;
  filterx_expr_unref(self->msg);
  filterx_expr_unref(self->columns.expr);
  if (self->columns.literals)
    g_ptr_array_unref(self->columns.literals);
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

  return TRUE;
error:
  g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
              "%s. %s", error_str, FILTERX_FUNC_PARSE_CSV_USAGE);
  return FALSE;
}

static gboolean
_add_literal_column(gsize index, FilterXExpr *column, gpointer user_data)
{
  GPtrArray *literal_columns = (GPtrArray *) user_data;

  if (!filterx_expr_is_literal(column))
    return FALSE;

  FilterXObject *column_name = filterx_expr_eval_typed(column);

  if (!filterx_object_is_type(column_name, &FILTERX_TYPE_NAME(string)))
    {
      filterx_object_unref(column_name);
      return FALSE;
    }

  g_ptr_array_add(literal_columns, column_name);
  return TRUE;
}

static gboolean
_extract_literal_columns(FilterXFunctionParseCSV *self, FilterXExpr *columns)
{
  GPtrArray *literal_columns = g_ptr_array_new_with_free_func((GDestroyNotify) filterx_object_unref);

  if (!filterx_literal_list_generator_foreach(columns, _add_literal_column, literal_columns))
    {
      g_ptr_array_unref(literal_columns);
      return FALSE;
    }

  self->columns.literals = literal_columns;
  return TRUE;
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

  FilterXExpr *columns = _extract_columns_expr(args, error);
  if (filterx_expr_is_literal_list_generator(columns) && _extract_literal_columns(self, columns))
    filterx_expr_unref(columns);
  else
    self->columns.expr = columns;

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

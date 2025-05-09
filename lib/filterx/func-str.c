/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Szilard Parrag
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

#include "filterx/func-str.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-message-value.h"
#include "filterx/object-list-interface.h"
#include "scratch-buffers.h"
#include "str-utils.h"

#define FILTERX_FUNC_STARTSWITH_USAGE "Usage: startswith(string, prefix, ignorecase=true)" \
"or startswith(string, [prefix_1, prefix_2, ..], ignorecase=true)"

#define FILTERX_FUNC_ENDSWITH_USAGE "Usage: endswith(string, suffix, ignorecase=true)" \
"or endswith(string, [suffix_1, suffix_2, ..], ignorecase=true)"

#define FILTERX_FUNC_INCLUDES_USAGE "Usage: includes(string, substring, ignorecase=true)" \
"or includes(string, [substring_1, substring_2, ..], ignorecase=true)"

#define FILTERX_FUNC_STRCASECMP_USAGE "Usage: strcasecmp(string, string)"

typedef struct _FilterXStringWithCache
{
  FilterXExpr *expr;
  gchar *str_value;
  gsize str_len;
} FilterXStringWithCache;

typedef gboolean (*FilterXExprAffixProcessFunc)(const gchar *haystack, gsize haystack_len, const gchar *needle,
                                                gsize needle_len);
typedef struct _FilterXExprAffix
{
  FilterXFunction super;
  gboolean ignore_case;
  FilterXExpr *haystack;
  struct
  {
    FilterXExpr *expr;
    GPtrArray *cached_strings;
  } needle;
  FilterXExprAffixProcessFunc process;
} FilterXExprAffix;

typedef struct _FilterXStrcasecmp
{
  FilterXFunction super;

  union
  {
    GString *literal;
    FilterXExpr *expr;
  } a;
  guint8 a_literal:1;

  union
  {
    GString *literal;
    FilterXExpr *expr;
  } b;
  guint8 b_literal:1;

} FilterXStrcasecmp;

static gboolean
_format_str_obj(FilterXObject *obj, const gchar **str, gsize *str_len)
{
  return filterx_object_extract_string_ref(obj, str, str_len);
}

static inline void
_do_casefold(const gchar **str, gsize *str_len)
{
  GString *buf = scratch_buffers_alloc();

  g_string_set_size(buf, *str_len);
  for (gsize i = 0; i < (*str_len); i++)
    buf->str[i] = ch_toupper((*str)[i]);
  *str = buf->str;
}

static gboolean
_expr_format(FilterXExpr *expr, const gchar **str, gsize *str_len, gboolean ignore_case)
{
  FilterXObject *obj = filterx_expr_eval(expr);
  gboolean result = FALSE;

  if (!obj)
    return FALSE;

  if (!_format_str_obj(obj, str, str_len))
    {
      filterx_eval_push_error("failed to extract string value, repr() failed", expr, obj);
      goto exit;
    }

  if (ignore_case)
    _do_casefold(str, str_len);

  result = TRUE;
exit:
  filterx_object_unref(obj);
  return result;
}

static gboolean
_obj_format(FilterXObject *obj, const gchar **str, gsize *str_len, gboolean ignore_case)
{
  gboolean result = FALSE;
  if (!_format_str_obj(obj, str, str_len))
    {
      filterx_eval_push_error("failed to extract string value, repr() failed", NULL, obj);
      goto exit;
    }

  if (ignore_case)
    _do_casefold(str, str_len);
  result = TRUE;
exit:
  return result;
}

static gboolean
_string_with_cache_fill_cache(FilterXStringWithCache *self, gboolean ignore_case)
{
  if (!filterx_expr_is_literal(self->expr))
    return TRUE;

  const gchar *str = NULL;
  gsize str_len = 0;
  if (!_expr_format(self->expr, &str, &str_len, ignore_case))
    return FALSE;

  self->str_value = g_strndup(str, str_len);
  self->str_len = str_len;
  return TRUE;
}

static FilterXStringWithCache *
_string_with_cache_new(FilterXExpr *expr, gboolean ignore_case)
{
  FilterXStringWithCache *self = g_new0(FilterXStringWithCache, 1);
  self->expr = filterx_expr_ref(expr);
  self->expr = filterx_expr_optimize(self->expr);

  if (!_string_with_cache_fill_cache(self, ignore_case))
    return NULL;

  return self;
}

static void
_string_with_cache_free(FilterXStringWithCache *self)
{
  filterx_expr_unref(self->expr);
  g_free(self->str_value);
  g_free(self);
}

static gboolean
_string_with_cache_get_string_value(FilterXStringWithCache *self, gboolean ignore_case, const gchar **dst,
                                    gsize *len)
{
  if (self->str_value)
    {
      *dst = self->str_value;
      *len = self->str_len;
      return TRUE;
    }

  const gchar *str;
  gsize str_len;
  if (!_expr_format(self->expr, &str, &str_len, ignore_case))
    return FALSE;

  *dst = str;
  *len = str_len;
  return TRUE;
}

static gboolean
_cache_needle(gsize index, FilterXExpr *value, gpointer user_data)
{
  gboolean *ignore_case = ((gpointer *)user_data)[0];
  GPtrArray *cached_strings = ((gpointer *) user_data)[1];

  FilterXStringWithCache *obj_with_cache =  _string_with_cache_new(value, *ignore_case);
  if(!obj_with_cache)
    return FALSE;

  g_ptr_array_add(cached_strings, obj_with_cache);
  return TRUE;
}

static gboolean
_expr_affix_cache_needle(FilterXExprAffix *self)
{
  if (filterx_expr_is_literal(self->needle.expr))
    {
      FilterXStringWithCache *obj_with_cache =  _string_with_cache_new(self->needle.expr, self->ignore_case);
      if (!obj_with_cache)
        goto error;
      g_ptr_array_add(self->needle.cached_strings, obj_with_cache);
    }
  else if (filterx_expr_is_literal_list(self->needle.expr))
    {
      gpointer user_data[] = {&self->ignore_case, self->needle.cached_strings};
      if (!filterx_literal_list_foreach(self->needle.expr, _cache_needle, user_data))
        goto error;
    }

  return TRUE;

error:
  g_ptr_array_remove_range(self->needle.cached_strings, 0, self->needle.cached_strings->len);
  return FALSE;
}

static gboolean
_expr_affix_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXExprAffix *self = (FilterXExprAffix *) s;

  if (!filterx_expr_init(self->haystack, cfg))
    return FALSE;

  if (!filterx_expr_init(self->needle.expr, cfg))
    {
      filterx_expr_deinit(self->haystack, cfg);
      return FALSE;
    }

  return filterx_function_init_method(&self->super, cfg);
}

static void
_expr_affix_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXExprAffix *self = (FilterXExprAffix *) s;

  filterx_expr_deinit(self->haystack, cfg);
  filterx_expr_deinit(self->needle.expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_expr_affix_free(FilterXExpr *s)
{
  FilterXExprAffix *self = (FilterXExprAffix *) s;
  filterx_expr_unref(self->haystack);
  filterx_expr_unref(self->needle.expr);
  g_ptr_array_free(self->needle.cached_strings, TRUE);

  filterx_function_free_method(&self->super);
}

static FilterXObject *
_eval_against_literal_needles(FilterXExprAffix *self, const gchar *haystack, gsize haystack_len)
{
  gboolean matches = FALSE;
  for (gsize i = 0; i < self->needle.cached_strings->len && !matches; i++)
    {
      FilterXStringWithCache *current_needle = g_ptr_array_index(self->needle.cached_strings, i);
      const gchar *needle_str;
      gsize needle_len;

      if (!_string_with_cache_get_string_value(current_needle, self->ignore_case, &needle_str, &needle_len))
        return NULL;

      matches = self->process(haystack, haystack_len, needle_str, needle_len);
    }
  return filterx_boolean_new(matches);
}

static FilterXObject *
_eval_against_needle_expr_list(FilterXExprAffix *self, const gchar *haystack, gsize haystack_len,
                               FilterXObject *needle_obj)
{
  FilterXObject *list_needle = filterx_ref_unwrap_ro(needle_obj);

  if (!filterx_object_is_type(list_needle, &FILTERX_TYPE_NAME(list)))
    return NULL;

  guint64 len;
  filterx_object_len(needle_obj, &len);

  if (len == 0)
    return NULL;

  gboolean matches = FALSE;
  for (gsize i = 0; i < len && !matches; i++)
    {
      FilterXObject *elem = filterx_list_get_subscript(list_needle, i);
      const gchar *current_needle;
      gsize current_needle_len;

      gboolean res = _obj_format(elem, &current_needle, &current_needle_len, self->ignore_case);
      filterx_object_unref(elem);

      if (!res)
        return NULL;

      matches = self->process(haystack, haystack_len, current_needle, current_needle_len);
    }
  return filterx_boolean_new(matches);
}

static FilterXObject *
_eval_against_needle_expr_single(FilterXExprAffix *self, const gchar *haystack, gsize haystack_len,
                                 FilterXObject *needle_obj)
{
  const gchar *needle;
  gsize needle_len;

  if (_obj_format(needle_obj, &needle, &needle_len, self->ignore_case))
    {
      return filterx_boolean_new(self->process(haystack, haystack_len, needle, needle_len));
    }
  return NULL;
}

static FilterXObject *
_eval_against_needle_expr(FilterXExprAffix *self, const gchar *haystack, gsize haystack_len)
{
  FilterXObject *needle_obj = filterx_expr_eval(self->needle.expr);
  if (!needle_obj)
    return NULL;

  FilterXObject *result = _eval_against_needle_expr_single(self, haystack, haystack_len, needle_obj);
  if (!result)
    result = _eval_against_needle_expr_list(self, haystack, haystack_len, needle_obj);
  if (!result)
    filterx_eval_push_error("failed to evaluate needle, expects a string value or a list of strings", self->needle.expr,
                            needle_obj);

  filterx_object_unref(needle_obj);
  return result;
}

static FilterXObject *
_expr_affix_eval(FilterXExpr *s)
{
  FilterXExprAffix *self = (FilterXExprAffix *)s;
  FilterXObject *result = NULL;

  ScratchBuffersMarker marker;
  scratch_buffers_mark(&marker);

  const gchar *haystack;
  gsize haystack_len;
  if (!_expr_format(self->haystack, &haystack, &haystack_len, self->ignore_case))
    goto exit;

  if (self->needle.cached_strings->len != 0)
    result = _eval_against_literal_needles(self, haystack, haystack_len);
  else
    result = _eval_against_needle_expr(self, haystack, haystack_len);

exit:
  scratch_buffers_reclaim_marked(marker);
  return result;

}

static FilterXExpr *
_expr_affix_optimize(FilterXExpr *s)
{
  FilterXExprAffix *self = (FilterXExprAffix *) s;

  self->haystack = filterx_expr_optimize(self->haystack);
  self->needle.expr = filterx_expr_optimize(self->needle.expr);

  if (!_expr_affix_cache_needle(self))
    goto exit;

  if (!filterx_expr_is_literal(self->haystack))
    goto exit;

  FilterXObject *result = _expr_affix_eval(s);
  if (result)
    return filterx_literal_new(result);

exit:
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_extract_args(FilterXExprAffix *self, FilterXFunctionArgs *args, GError **error, const gchar *function_usage)
{

  if (filterx_function_args_len(args) != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. %s", function_usage);
      return FALSE;
    }

  gboolean exists, eval_error;
  gboolean value = filterx_function_args_get_named_literal_boolean(args, "ignorecase", &exists, &eval_error);

  if (eval_error)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "ignorecase argument must be boolean literal. %s", function_usage);
      return FALSE;
    }
  if (!exists)
    self->ignore_case = FALSE;
  else
    self->ignore_case = value;

  self->haystack = filterx_function_args_get_expr(args, 0);
  g_assert(self->haystack);

  self->needle.expr = filterx_function_args_get_expr(args, 1);
  g_assert(self->needle.expr);

  return TRUE;
}

static FilterXExpr *
_function_affix_new(FilterXFunctionArgs *args,
                    const gchar *affix_name,
                    FilterXExprAffixProcessFunc process_func,
                    const gchar *usage,
                    GError **error)
{
  FilterXExprAffix *self = g_new0(FilterXExprAffix, 1);

  filterx_function_init_instance(&self->super, affix_name);
  self->super.super.eval = _expr_affix_eval;
  self->super.super.optimize = _expr_affix_optimize;
  self->super.super.init = _expr_affix_init;
  self->super.super.deinit = _expr_affix_deinit;
  self->super.super.free_fn = _expr_affix_free;

  self->needle.cached_strings = g_ptr_array_new_with_free_func((GDestroyNotify) _string_with_cache_free);
  self->process = process_func;


  if (!_extract_args(self, args, error, usage))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

static gboolean
_function_startswith_process(const gchar *haystack, gsize haystack_len, const gchar *needle, gsize needle_len)
{
  if (needle_len > haystack_len)
    return FALSE;
  return memcmp(haystack, needle, needle_len) == 0;
}

FilterXExpr *
filterx_function_startswith_new(FilterXFunctionArgs *args, GError **error)
{
  return _function_affix_new(args, "startswith", _function_startswith_process,
                             FILTERX_FUNC_STARTSWITH_USAGE, error);
}

static gboolean
_function_endswith_process(const gchar *haystack, gsize haystack_len, const gchar *needle, gsize needle_len)
{
  if (needle_len > haystack_len)
    return FALSE;
  return memcmp(haystack + haystack_len - needle_len, needle, needle_len) == 0;
}

FilterXExpr *
filterx_function_endswith_new(FilterXFunctionArgs *args, GError **error)
{
  return _function_affix_new(args, "endswith", _function_endswith_process, FILTERX_FUNC_ENDSWITH_USAGE,
                             error);
}

static gboolean
_function_includes_process(const gchar *haystack, gsize haystack_len, const gchar *needle, gsize needle_len)
{
  if (needle_len > haystack_len)
    return FALSE;
  return strstr(haystack, needle) != NULL;
}

FilterXExpr *
filterx_function_includes_new(FilterXFunctionArgs *args, GError **error)
{
  return _function_affix_new(args, "includes", _function_includes_process, FILTERX_FUNC_INCLUDES_USAGE,
                             error);
}

static FilterXObject *
_strcasecmp_eval(FilterXExpr *s)
{
  FilterXStrcasecmp *self = (FilterXStrcasecmp *) s;
  FilterXObject *result = NULL;
  FilterXObject *a_obj = NULL, *b_obj = NULL;

  gsize a_str_len = 0;
  const gchar *a_str = NULL;
  if (!self->a_literal)
    {
      a_obj = filterx_expr_eval(self->a.expr);
      if (!a_obj)
        goto exit;

      if (!filterx_object_extract_string_ref(a_obj, &a_str, &a_str_len))
        {
          filterx_eval_push_error("failed to extract string value", self->a.expr, a_obj);
          goto exit;
        }
    }
  else
    {
      a_str = self->a.literal->str;
      a_str_len = self->a.literal->len;
    }

  const gchar *b_str = NULL;
  gsize b_str_len = 0;
  if (!self->b_literal)
    {
      b_obj = filterx_expr_eval(self->b.expr);

      if (!b_obj)
        goto exit;

      if (!filterx_object_extract_string_ref(b_obj, &b_str, &b_str_len))
        {
          filterx_eval_push_error("failed to extract string value", self->b.expr, b_obj);
          filterx_object_unref(b_obj);
          goto exit;
        }
    }
  else
    {
      b_str = self->b.literal->str;
      b_str_len = self->b.literal->len;
    }

  gsize cmp_len = MIN(a_str_len, b_str_len);

  /* TODO: utf-8 support together with all func-str-transform */
  gint cmp = g_ascii_strncasecmp(a_str, b_str, cmp_len);
  if (cmp == 0)
    cmp = a_str_len - b_str_len;
  result = filterx_integer_new(cmp);

  filterx_object_unref(b_obj);

exit:
  filterx_object_unref(a_obj);
  return result;
}

static inline GString *
_extract_literal(FilterXExpr *expr)
{
  FilterXObject *literal = filterx_expr_eval(expr);
  g_assert(literal);

  const gchar *str = NULL;
  gsize str_len = 0;
  if (!filterx_object_extract_string_ref(literal, &str, &str_len))
    return NULL;

  return g_string_new_len(str, str_len);
}

static FilterXExpr *
_strcasecmp_optimize(FilterXExpr *s)
{
  FilterXStrcasecmp *self = (FilterXStrcasecmp *) s;

  self->a.expr = filterx_expr_optimize(self->a.expr);
  self->b.expr = filterx_expr_optimize(self->b.expr);

  if (filterx_expr_is_literal(self->a.expr) && filterx_expr_is_literal(self->b.expr))
    {
      FilterXObject *result = _strcasecmp_eval(s);
      if (!result)
        goto exit;

      return filterx_literal_new(result);
    }

  if (filterx_expr_is_literal(self->a.expr))
    {
      GString *literal = _extract_literal(self->a.expr);
      if (!literal)
        goto exit;

      filterx_expr_unref(self->a.expr);
      self->a.literal = literal;
      self->a_literal = TRUE;
      goto exit;
    }

  if (filterx_expr_is_literal(self->b.expr))
    {
      GString *literal = _extract_literal(self->b.expr);
      if (!literal)
        goto exit;

      filterx_expr_unref(self->b.expr);
      self->b.literal = literal;
      self->b_literal = TRUE;
    }

exit:
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_strcasecmp_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXStrcasecmp *self = (FilterXStrcasecmp *) s;

  if (!self->a_literal && !filterx_expr_init(self->a.expr, cfg))
    return FALSE;

  if (!self->b_literal && !filterx_expr_init(self->b.expr, cfg))
    {
      filterx_expr_deinit(self->a.expr, cfg);
      return FALSE;
    }

  return filterx_function_init_method(&self->super, cfg);
}

static void
_strcasecmp_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXStrcasecmp *self = (FilterXStrcasecmp *) s;

  if (!self->a_literal)
    filterx_expr_deinit(self->a.expr, cfg);
  if (!self->b_literal)
    filterx_expr_deinit(self->b.expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_strcasecmp_free(FilterXExpr *s)
{
  FilterXStrcasecmp *self = (FilterXStrcasecmp *) s;
  if (self->a_literal)
    g_string_free(self->a.literal, TRUE);
  else
    filterx_expr_unref(self->a.expr);

  if (self->b_literal)
    g_string_free(self->b.literal, TRUE);
  else
    filterx_expr_unref(self->b.expr);

  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_function_strcasecmp_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXStrcasecmp *self = g_new0(FilterXStrcasecmp, 1);

  filterx_function_init_instance(&self->super, "strcasecmp");
  self->super.super.eval = _strcasecmp_eval;
  self->super.super.optimize = _strcasecmp_optimize;
  self->super.super.init = _strcasecmp_init;
  self->super.super.deinit = _strcasecmp_deinit;
  self->super.super.free_fn = _strcasecmp_free;

  if (filterx_function_args_len(args) != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. %s", FILTERX_FUNC_STRCASECMP_USAGE);
      goto error;
    }

  self->a.expr = filterx_function_args_get_expr(args, 0);
  g_assert(self->a.expr);

  self->b.expr = filterx_function_args_get_expr(args, 1);
  g_assert(self->b.expr);

  if(!filterx_function_args_check(args, error))
    goto error;


  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

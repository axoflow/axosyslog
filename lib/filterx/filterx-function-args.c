#include "filterx/filterx-function-args.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/object-primitive.h"
#include "mainloop.h"


/* Takes reference of value */
FilterXFunctionArg *
filterx_function_arg_new(const gchar *name, FilterXExpr *value)
{
  FilterXFunctionArg *self = g_new0(FilterXFunctionArg, 1);

  self->name = g_strdup(name);
  self->value = value;

  return self;
}

void
filterx_function_arg_free(FilterXFunctionArg *self)
{
  g_free(self->name);
  filterx_expr_unref(self->value);
  g_free(self);
}

/* FilterXFunctionArgs */

struct _FilterXFunctionArgs
{
  GPtrArray *positional_args;
  GHashTable *named_args;
};

static FilterXExpr *
_args_get_expr(FilterXFunctionArgs *self, guint64 index)
{
  /* 'retrieved' is not thread-safe */
  main_loop_assert_main_thread();

  if (self->positional_args->len <= index)
    return NULL;

  FilterXFunctionArg *arg = g_ptr_array_index(self->positional_args, index);
  arg->retrieved = TRUE;
  return (FilterXExpr *) arg->value;
}

guint64
filterx_function_args_len(FilterXFunctionArgs *self)
{
  return self->positional_args->len;
}

gboolean
filterx_function_args_empty(FilterXFunctionArgs *self)
{
  return self->positional_args->len == 0 && g_hash_table_size(self->named_args) == 0;
}

FilterXExpr *
filterx_function_args_get_expr(FilterXFunctionArgs *self, guint64 index)
{
  return filterx_expr_ref(_args_get_expr(self, index));
}

static const gchar *
_get_literal_string_from_expr(FilterXExpr *expr, gsize *len)
{
  FilterXObject *obj = NULL;
  const gchar *str = NULL;

  if (!filterx_expr_is_literal(expr))
    goto error;

  obj = filterx_expr_eval(expr);
  if (!obj)
    goto error;

  /* Literal message values don't exist, so we don't need to use the extractor. */
  str = filterx_string_get_value_ref(obj, len);

  /*
   * We can unref both obj and expr, the underlying string will be kept alive as long as the literal expr is alive,
   * which is kept alive in either in the positional or named args container.
   */

error:
  filterx_object_unref(obj);
  return str;
}

const gchar *
filterx_function_args_get_literal_string(FilterXFunctionArgs *self, guint64 index, gsize *len)
{
  FilterXExpr *expr = _args_get_expr(self, index);
  if (!expr)
    return NULL;

  return _get_literal_string_from_expr(expr, len);
}

gboolean
filterx_function_args_is_literal_null(FilterXFunctionArgs *self, guint64 index)
{
  FilterXExpr *expr = _args_get_expr(self, index);
  if (!expr)
    return FALSE;

  if (!filterx_expr_is_literal(expr))
    return FALSE;

  FilterXObject *obj = filterx_expr_eval(expr);
  if (!obj)
    return FALSE;

  gboolean is_literal_null = filterx_object_is_type(obj, &FILTERX_TYPE_NAME(null));
  filterx_object_unref(obj);

  return is_literal_null;
}

FilterXExpr *
_args_get_named_expr(FilterXFunctionArgs *self, const gchar *name)
{
  /* 'retrieved' is not thread-safe */
  main_loop_assert_main_thread();

  FilterXFunctionArg *arg = g_hash_table_lookup(self->named_args, name);
  if (!arg)
    return NULL;
  arg->retrieved = TRUE;
  return arg->value;
}

FilterXExpr *
filterx_function_args_get_named_expr(FilterXFunctionArgs *self, const gchar *name)
{
  return filterx_expr_ref(_args_get_named_expr(self, name));
}

FilterXObject *
filterx_function_args_get_named_literal_object(FilterXFunctionArgs *self, const gchar *name, gboolean *exists)
{
  FilterXExpr *expr = _args_get_named_expr(self, name);
  *exists = !!expr;
  if (!expr)
    return NULL;

  if (!filterx_expr_is_literal(expr))
    return NULL;

  return filterx_expr_eval(expr);
}


const gchar *
filterx_function_args_get_named_literal_string(FilterXFunctionArgs *self, const gchar *name, gsize *len,
                                               gboolean *exists)
{
  FilterXExpr *expr = _args_get_named_expr(self, name);
  *exists = !!expr;
  if (!expr)
    return NULL;

  return _get_literal_string_from_expr(expr, len);
}

gboolean
filterx_function_args_get_named_literal_boolean(FilterXFunctionArgs *self, const gchar *name, gboolean *exists,
                                                gboolean *error)
{
  GenericNumber gn = filterx_function_args_get_named_literal_generic_number(self, name, exists, error);
  if (!(*exists) || *error)
    return 0;

  if (gn.type != GN_INT64)
    {
      *error = TRUE;
      return 0;
    }

  return !!gn_as_int64(&gn);
}

gint64
filterx_function_args_get_named_literal_integer(FilterXFunctionArgs *self, const gchar *name, gboolean *exists,
                                                gboolean *error)
{
  GenericNumber gn = filterx_function_args_get_named_literal_generic_number(self, name, exists, error);
  if (!(*exists) || *error)
    return 0;

  if (gn.type != GN_INT64)
    {
      *error = TRUE;
      return 0;
    }

  return gn_as_int64(&gn);
}

gdouble
filterx_function_args_get_named_literal_double(FilterXFunctionArgs *self, const gchar *name, gboolean *exists,
                                               gboolean *error)
{
  GenericNumber gn = filterx_function_args_get_named_literal_generic_number(self, name, exists, error);
  if (!(*exists) || *error)
    return 0;

  if (gn.type != GN_DOUBLE)
    {
      *error = TRUE;
      return 0;
    }

  return gn_as_double(&gn);
}

GenericNumber
filterx_function_args_get_named_literal_generic_number(FilterXFunctionArgs *self, const gchar *name, gboolean *exists,
                                                       gboolean *error)
{
  *error = FALSE;
  GenericNumber value;
  gn_set_nan(&value);

  FilterXExpr *expr = _args_get_named_expr(self, name);
  *exists = !!expr;
  if (!expr)
    return value;

  FilterXObject *obj = NULL;

  if (!filterx_expr_is_literal(expr))
    goto error;

  obj = filterx_expr_eval(expr);
  if (!obj)
    goto error;

  if (!filterx_object_is_type(obj, &FILTERX_TYPE_NAME(primitive)))
    goto error;

  /* Literal message values don't exist, so we don't need to use the extractor. */
  value = filterx_primitive_get_value(obj);
  goto success;

error:
  *error = TRUE;
success:
  filterx_object_unref(obj);
  return value;
}

gboolean
filterx_function_args_check(FilterXFunctionArgs *self, GError **error)
{
  for (gint i = 0; i < self->positional_args->len; i++)
    {
      FilterXFunctionArg *arg = g_ptr_array_index(self->positional_args, i);

      /* The function must retrieve all positional arguments and indicate an
       * error if there are too many or too few of them.  If the function
       * did not touch all such arguments, that's a programming error
       */
      g_assert(arg->retrieved);
    }
  GHashTableIter iter;
  g_hash_table_iter_init(&iter, self->named_args);

  gpointer k, v;
  while (g_hash_table_iter_next(&iter, &k, &v))
    {
      const gchar *name = k;
      FilterXFunctionArg *arg = v;

      if (!arg->retrieved)
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_UNEXPECTED_ARGS,
                      "unexpected argument \"%s\"", name);
          return FALSE;
        }
    }
  return TRUE;
}

/* Takes reference of positional_args. */
FilterXFunctionArgs *
filterx_function_args_new(GList *args, GError **error)
{
  FilterXFunctionArgs *self = g_new0(FilterXFunctionArgs, 1);

  self->named_args = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) filterx_function_arg_free);
  self->positional_args = g_ptr_array_new_full(8, (GDestroyNotify) filterx_function_arg_free);

  gboolean has_named = FALSE;
  for (GList *elem = args; elem; elem = elem->next)
    {
      FilterXFunctionArg *arg = (FilterXFunctionArg *) elem->data;
      if (!arg->name)
        {
          if (has_named)
            {
              g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                          "cannot set positional argument after a named argument");
              filterx_function_args_free(self);
              self = NULL;
              goto exit;
            }
          g_ptr_array_add(self->positional_args, arg);
        }
      else
        {
          g_hash_table_insert(self->named_args, g_strdup(arg->name), arg);
          has_named = TRUE;
        }
    }

exit:
  g_list_free(args);
  return self;
}

void
filterx_function_args_free(FilterXFunctionArgs *self)
{
  /* I had an assert here that aborted if filterx_function_args_check() was
   * not called.  Unfortunately that needs further work, so I removed it:
   * simple functions may not be evaluated at all, and in those cases their
   * arguments will never be checked. */

  g_ptr_array_free(self->positional_args, TRUE);
  g_hash_table_unref(self->named_args);
  g_free(self);
}

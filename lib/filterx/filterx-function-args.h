#ifndef FILTERX_EXPR_FUNCTION_ARGS_H_INCLUDED
#define FILTERX_EXPR_FUNCTION_ARGS_H_INCLUDED

#include "filterx/filterx-expr.h"
#include "filterx/filterx-function-error.h"
#include "generic-number.h"

typedef struct _FilterXFunctionArg
{
  gchar *name;
  FilterXExpr *value;
  gboolean retrieved;
} FilterXFunctionArg;

FilterXFunctionArg *filterx_function_arg_new(const gchar *name, FilterXExpr *value);
void filterx_function_arg_free(FilterXFunctionArg *self);

typedef struct _FilterXFunctionArgs FilterXFunctionArgs;

guint64 filterx_function_args_len(FilterXFunctionArgs *self);
gboolean filterx_function_args_empty(FilterXFunctionArgs *self);
FilterXExpr *filterx_function_args_get_expr(FilterXFunctionArgs *self, guint64 index);
const gchar *filterx_function_args_get_literal_string(FilterXFunctionArgs *self, guint64 index, gsize *len);
gboolean filterx_function_args_is_literal_null(FilterXFunctionArgs *self, guint64 index);
FilterXExpr *filterx_function_args_get_named_expr(FilterXFunctionArgs *self, const gchar *name);
FilterXObject *filterx_function_args_get_named_literal_object(FilterXFunctionArgs *self, const gchar *name,
    gboolean *exists);
const gchar *filterx_function_args_get_named_literal_string(FilterXFunctionArgs *self, const gchar *name,
                                                            gsize *len, gboolean *exists);
gboolean filterx_function_args_get_named_literal_boolean(FilterXFunctionArgs *self, const gchar *name,
                                                         gboolean *exists, gboolean *error);
gint64 filterx_function_args_get_named_literal_integer(FilterXFunctionArgs *self, const gchar *name,
                                                       gboolean *exists, gboolean *error);
gdouble filterx_function_args_get_named_literal_double(FilterXFunctionArgs *self, const gchar *name,
                                                       gboolean *exists, gboolean *error);
GenericNumber filterx_function_args_get_named_literal_generic_number(FilterXFunctionArgs *self, const gchar *name,
    gboolean *exists, gboolean *error);
gboolean filterx_function_args_check(FilterXFunctionArgs *self, GError **error);

FilterXFunctionArgs *filterx_function_args_new(GList *args, GError **error);
void filterx_function_args_free(FilterXFunctionArgs *self);


#endif

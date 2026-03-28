/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/func-digest.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "filterx/expr-function.h"
#include "compat/openssl_support.h"
#include "str-format.h"

#include <openssl/evp.h>

#define FILTERX_FUNC_DIGEST_ARG_NAME_ALG "alg"
#define FILTERX_FUNC_DIGEST_DEFAULT_ALG "sha256"
#define FILTERX_FUNC_DIGEST_USAGE "Usage: digest(string_or_bytes, alg=\"sha256\")"

static void
_compute_digest(const gchar *input, gsize input_len, const EVP_MD *md, guchar hash[EVP_MAX_MD_SIZE], guint *md_len)
{
  DECLARE_EVP_MD_CTX(ctx);
  EVP_MD_CTX_init(ctx);
  EVP_DigestInit_ex(ctx, md, NULL);
  EVP_DigestUpdate(ctx, input, input_len);
  EVP_DigestFinal_ex(ctx, hash, md_len);
  EVP_MD_CTX_cleanup(ctx);
  EVP_MD_CTX_destroy(ctx);
}

static FilterXObject *
_do_digest_hex(const gchar *input, gsize input_len, const EVP_MD *md)
{
  guchar hash[EVP_MAX_MD_SIZE];
  guint md_len;
  _compute_digest(input, input_len, md, hash, &md_len);

  gchar *hex = g_malloc(md_len * 2 + 1);
  format_hex_string(hash, md_len, hex, md_len * 2 + 1);
  return filterx_string_new_take(hex, (gssize)(md_len * 2));
}

static FilterXObject *
_do_digest_bytes(const gchar *input, gsize input_len, const EVP_MD *md)
{
  guchar hash[EVP_MAX_MD_SIZE];
  guint md_len;
  _compute_digest(input, input_len, md, hash, &md_len);

  return filterx_bytes_new((gchar *) hash, (gssize) md_len);
}

static FilterXObject *
_digest_simple(FilterXExpr *s, FilterXObject *args[], gsize args_len, const EVP_MD *md)
{
  if (args == NULL || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Requires exactly one argument");
      return NULL;
    }

  const gchar *input;
  gsize input_len;

  if (!filterx_object_extract_string_ref(args[0], &input, &input_len) &&
      !filterx_object_extract_bytes_ref(args[0], &input, &input_len))
    {
      filterx_simple_function_argument_error(s, "Argument must be a string or bytes");
      return NULL;
    }

  return _do_digest_hex(input, input_len, md);
}

FilterXObject *
filterx_simple_function_md5(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  return _digest_simple(s, args, args_len, EVP_get_digestbyname("md5"));
}

FilterXObject *
filterx_simple_function_sha1(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  return _digest_simple(s, args, args_len, EVP_get_digestbyname("sha1"));
}

FilterXObject *
filterx_simple_function_sha256(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  return _digest_simple(s, args, args_len, EVP_get_digestbyname("sha256"));
}

FilterXObject *
filterx_simple_function_sha512(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  return _digest_simple(s, args, args_len, EVP_get_digestbyname("sha512"));
}

/* Constructor-based digest() with optional alg parameter */

typedef struct _FilterXFunctionDigest
{
  FilterXFunction super;
  FilterXExpr *input;
  const EVP_MD *md;
} FilterXFunctionDigest;

static FilterXObject *
_filterx_function_digest_eval(FilterXExpr *s)
{
  FilterXFunctionDigest *self = (FilterXFunctionDigest *) s;

  FilterXObject *input_obj = filterx_expr_eval(self->input);
  if (!input_obj)
    return NULL;

  const gchar *input;
  gsize input_len;

  if (!filterx_object_extract_string_ref(input_obj, &input, &input_len) &&
      !filterx_object_extract_bytes_ref(input_obj, &input, &input_len))
    {
      filterx_eval_push_error_info_printf(FILTERX_FUNC_DIGEST_USAGE, s,
                                          "Argument must be a string or bytes, got type: %s",
                                          input_obj->type->name);
      filterx_object_unref(input_obj);
      return NULL;
    }

  FilterXObject *result = _do_digest_bytes(input, input_len, self->md);
  filterx_object_unref(input_obj);
  return result;
}

static gboolean
_digest_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionDigest *self = (FilterXFunctionDigest *) s;
  return filterx_expr_visit(s, &self->input, f, user_data);
}

static void
_filterx_function_digest_free(FilterXExpr *s)
{
  FilterXFunctionDigest *self = (FilterXFunctionDigest *) s;
  filterx_expr_unref(self->input);
  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_function_digest_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionDigest *self = g_new0(FilterXFunctionDigest, 1);
  filterx_function_init_instance(&self->super, "digest", FXE_READ);
  self->super.super.eval = _filterx_function_digest_eval;
  self->super.super.walk_children = _digest_walk;
  self->super.super.free_fn = _filterx_function_digest_free;

  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  FILTERX_FUNC_DIGEST_USAGE);
      goto error;
    }

  self->input = filterx_function_args_get_expr(args, 0);

  gboolean alg_exists;
  const gchar *alg = filterx_function_args_get_named_literal_string(args, FILTERX_FUNC_DIGEST_ARG_NAME_ALG, NULL,
                     &alg_exists);
  if (!alg_exists)
    alg = FILTERX_FUNC_DIGEST_DEFAULT_ALG;

  self->md = EVP_get_digestbyname(alg);
  if (!self->md)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "digest(): unknown algorithm: %s", alg);
      goto error;
    }

  if (!filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

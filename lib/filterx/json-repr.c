/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2024 László Várady
 * Copyright (c) 2024 Attila Szakacs
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "filterx/json-repr.h"
#include "filterx/object-null.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-list.h"
#include "filterx/filterx-sequence.h"
#include "filterx/object-extractor.h"
#include "filterx/object-message-value.h"
#include "filterx/filterx-eval.h"
#include "generic-number.h"
#include "parse-number.h"
#include "scratch-buffers.h"
#include "utf8utils.h"
#include "tls-support.h"
#include "str-utils.h"

#include "logmsg/type-hinting.h"

#define JSMN_STATIC 1
#define JSMN_STRICT 1
#define JSMN_PARENT_LINKS 1
#include "jsmn.h"

TLS_BLOCK_START
{
  jsmntok_t *jsmn_tokens;
  gint jsmn_tokens_len;
}
TLS_BLOCK_END;

#define jsmn_tokens      __tls_deref(jsmn_tokens)
#define jsmn_tokens_len  __tls_deref(jsmn_tokens_len)

/* JSON parsing */

static FilterXObject *
filterx_object_from_jsmn_tokens(const gchar *json_text, gsize json_len, jsmntok_t **tokens, jsmntok_t *sentinel);

static FilterXObject *
_convert_from_json_object(const gchar *json_text, gsize json_len,
                          jsmntok_t **tokens, jsmntok_t *sentinel)
{
  FilterXObject *res = filterx_dict_new();

  filterx_object_cow_prepare(&res);

  /* NOTE: skip object token */
  jsmntok_t *token = *tokens;
  gsize elements = token->size;
  token++;
  for (gint i = 0; i < elements && token < sentinel; i++)
    {
      FilterXObject *key = filterx_object_from_jsmn_tokens(json_text, json_len, &token, sentinel);
      if (!key)
        goto error;

      FilterXObject *value = filterx_object_from_jsmn_tokens(json_text, json_len, &token, sentinel);
      if (!value)
        {
          filterx_object_unref(key);
          goto error;
        }

      gboolean success = filterx_object_set_subscript(res, key, &value);
      filterx_object_unref(key);
      filterx_object_unref(value);
      if (!success)
        {
          goto error;
        }
    }
  *tokens = token;
  filterx_object_set_dirty(res, FALSE);
  return res;
error:
  filterx_object_unref(res);
  return NULL;
}

static FilterXObject *
_convert_from_json_array(const gchar *json_text, gsize json_len,
                         jsmntok_t **tokens, jsmntok_t *sentinel)
{
  FilterXObject *res = filterx_list_new();
  filterx_object_cow_prepare(&res);

  /* NOTE: skip list token */
  jsmntok_t *token = *tokens;
  gsize elements = token->size;
  token++;
  for (gint i = 0; i < elements && token < sentinel; i++)
    {
      FilterXObject *o = filterx_object_from_jsmn_tokens(json_text, json_len, &token, sentinel);
      if (!o)
        goto error;

      gboolean success = filterx_sequence_append(res, &o);
      filterx_object_unref(o);
      if (!success)
        {
          goto error;
        }
    }
  *tokens = token;
  filterx_object_set_dirty(res, FALSE);
  return res;
error:
  filterx_object_unref(res);
  return NULL;
}

static FilterXObject *
_convert_from_json_number(const gchar *json_text, gsize json_len, jsmntok_t *token)
{
  gchar buf[64];
  GenericNumber gn;

  gsize len = token->end - token->start;
  if (len > sizeof(buf) - 1)
    return NULL;
  memcpy(buf, &json_text[token->start], len);
  buf[len] = 0;

  if (!parse_generic_number(buf, &gn))
    return NULL;

  return filterx_primitive_new_from_gn(&gn);
}

static FilterXObject *
_convert_from_json_primitive(const gchar *json_text, gsize json_len,
                             jsmntok_t **tokens, jsmntok_t *sentinel)
{

  FilterXObject *result = NULL;
  jsmntok_t *token = *tokens;

  switch (json_text[token->start])
    {
    case 't':
      if (strn_eq_strz(&json_text[token->start], "true", token->end - token->start))
        result = filterx_boolean_new(TRUE);
      break;
    case 'f':
      if (strn_eq_strz(&json_text[token->start], "false", token->end - token->start))
        result = filterx_boolean_new(FALSE);
      break;
    case 'n':
      if (strn_eq_strz(&json_text[token->start], "null", token->end - token->start))
        result = filterx_null_new();
      break;
    default:
      result = _convert_from_json_number(json_text, json_len, token);
      break;
    }
  if (result)
    *tokens = token + 1;
  return result;
}


static FilterXObject *
_convert_from_json_string(const gchar *json_text, gsize json_len,
                          jsmntok_t **tokens, jsmntok_t *sentinel)
{

  FilterXObject *result = NULL;
  jsmntok_t *token = *tokens;

  if (token->flags & JSMN_STRING_NO_ESCAPE)
    {
      result = filterx_string_new(&json_text[token->start], token->end - token->start);
      filterx_string_mark_safe_without_json_escaping(result);
    }
  else
    {
      result = filterx_string_new_from_json_literal(&json_text[token->start], token->end - token->start);
    }
  if (result)
    *tokens = token + 1;
  return result;
}

static FilterXObject *
filterx_object_from_jsmn_tokens(const gchar *json_text, gsize json_len, jsmntok_t **tokens, jsmntok_t *sentinel)
{
  FilterXObject *result = NULL;

  jsmntok_t *token = *tokens;

  if (token >= sentinel)
    return NULL;

  switch (token->type)
    {
    case JSMN_OBJECT:
      result = _convert_from_json_object(json_text, json_len, &token, sentinel);
      break;
    case JSMN_ARRAY:
      result = _convert_from_json_array(json_text, json_len, &token, sentinel);
      break;
    case JSMN_STRING:
      g_assert(token->start <= token->end && token->end < json_len);
      result = _convert_from_json_string(json_text, json_len, &token, sentinel);
      break;
    case JSMN_PRIMITIVE:
      g_assert(token->start <= token->end && token->end < json_len);
      result = _convert_from_json_primitive(json_text, json_len, &token, sentinel);
      break;
    case JSMN_UNDEFINED:
      break;
    default:
      g_assert_not_reached();
    }
  *tokens = token;
  return result;
}

FilterXObject *
filterx_object_from_json_object(struct json_object *jso, GError **error)
{
  return NULL;
}


FilterXObject *
filterx_object_from_json(const gchar *repr, gssize repr_len, GError **error)
{
  const gint min_tokens = 256;
  const gint max_tokens = 65536;

  g_return_val_if_fail(error == NULL || (*error) == NULL, NULL);

  if (repr_len < 0)
    repr_len = strlen(repr);

  jsmn_parser parser;
  jsmn_init(&parser);

  if (jsmn_tokens == NULL)
    {
      jsmn_tokens_len = min_tokens;
      jsmn_tokens = g_new(jsmntok_t, jsmn_tokens_len);
    }

  gint r;
  gboolean try_again;
  do
    {
      try_again = FALSE;

      r = jsmn_parse(&parser, repr, repr_len, jsmn_tokens, jsmn_tokens_len);
      if (r == JSMN_ERROR_NOMEM && jsmn_tokens_len < max_tokens)
        {
          jsmn_tokens_len *= 2;
          jsmn_tokens = g_renew(jsmntok_t, jsmn_tokens, jsmn_tokens_len);
          try_again = TRUE;
        }
    }
  while (try_again);

  if (r < 0)
    {
      switch (r)
        {
        case JSMN_ERROR_NOMEM:
          g_set_error(error, FILTERX_JSON_ERROR, FILTERX_JSON_ERROR_TOO_LARGE,
                      "JSON text too large, number of tokens exceeds the maximum of %d tokens (size %ld bytes)",
                      max_tokens, repr_len);
          break;
        case JSMN_ERROR_INVAL:
          {
            const gint excerpt_len = 20;
            gint prologue_start = MAX((gint) parser.pos - excerpt_len, 0);
            if (prologue_start < 0)
              prologue_start = 0;
            gint prologue_len = excerpt_len;
            if (prologue_start + prologue_len > parser.pos)
              prologue_len = parser.pos - prologue_start;

            gint epilogue_len = excerpt_len;
            if (parser.pos + epilogue_len + 1 > repr_len)
              epilogue_len = repr_len - parser.pos - 1;
            g_set_error(error, FILTERX_JSON_ERROR, FILTERX_JSON_ERROR_INVALID,
                        "JSON parse error at %d, excerpt: %.*s>%c<%.*s",
                        parser.pos,
                        prologue_len, &repr[prologue_start],
                        repr[parser.pos],
                        epilogue_len, &repr[parser.pos+1]);
            break;
          }
        case JSMN_ERROR_PART:
          {
            const gint excerpt_len = 20;
            gint prologue_start = (gint) parser.pos - excerpt_len;
            if (prologue_start < 0)
              prologue_start = 0;
            gint prologue_len = excerpt_len;
            if (prologue_start + prologue_len > parser.pos)
              prologue_len = parser.pos - prologue_start;
            g_set_error(error, FILTERX_JSON_ERROR, FILTERX_JSON_ERROR_INCOMPLETE,
                        "JSON text incomplete, excerpt: %.*s",
                        prologue_len, &repr[prologue_start]);
            break;
          }
        default:
          g_assert_not_reached();
        }
      return NULL;
    }

  jsmntok_t *tokens = jsmn_tokens;
  FilterXObject *res = filterx_object_from_jsmn_tokens(repr, repr_len, &tokens, tokens + r);

  if (!res)
    {
      g_set_error(error, FILTERX_JSON_ERROR, FILTERX_JSON_ERROR_STORE_ERROR, "Invalid JSON, unrecognized token");
    }
  else if (tokens != jsmn_tokens + r)
    {
      g_set_error(error, FILTERX_JSON_ERROR, FILTERX_JSON_ERROR_STORE_ERROR, "Expected a single JSON object, multiple top-level objects found");
      filterx_object_unref(res);
      res = NULL;
    }

  return res;
}

FilterXObject *
filterx_parse_json_call(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (!args || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Incorrect number of arguments");
      return NULL;
    }
  FilterXObject *arg = args[0];
  const gchar *repr;
  gsize repr_len;
  if (!filterx_object_extract_string_ref(arg, &repr, &repr_len))
    {
      filterx_simple_function_argument_error(s, "Argument must be a string");
      return NULL;
    }
  GError *error = NULL;
  FilterXObject *res = filterx_object_from_json(repr, repr_len, &error);
  if (!res)
    {
      filterx_eval_push_error_info_printf("Error parsing JSON string", s, "%s", error->message);
      g_clear_error(&error);
      return NULL;
    }
  return res;
}

/* JSON formatting */

gboolean
filterx_object_to_json(FilterXObject *value, GString *result)
{
  return filterx_object_format_json_append(value, result);
}

static FilterXObject *
_format_json(FilterXObject *arg)
{
  FilterXObject *result = NULL;
  ScratchBuffersMarker marker;
  GString *result_string = scratch_buffers_alloc_and_mark(&marker);

  if (!filterx_object_to_json(arg, result_string))
    goto exit;

  result = filterx_string_new(result_string->str, result_string->len);

exit:
  scratch_buffers_reclaim_marked(marker);
  return result;
}

FilterXObject *
filterx_format_json_call(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (!args || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Incorrect number of arguments");
      return NULL;
    }

  FilterXObject *arg = args[0];
  return _format_json(arg);
}

void
filterx_json_repr_thread_init(void)
{
}

void
filterx_json_repr_thread_deinit(void)
{
  g_free(jsmn_tokens);
  jsmn_tokens = NULL;
}

GQuark
filterx_json_error_quark(void)
{
  return g_quark_from_static_string("filterx-json-error-quark");
}

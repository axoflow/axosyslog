/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-globals.h"
#include "filterx/filterx-config.h"
#include "filterx/filterx-eval.h"
#include "str-utils.h"
#include "scratch-buffers.h"
#include "str-format.h"
#include "utf8utils.h"

FilterXObject *fx_string_cache[FILTERX_STRING_CACHE_SIZE];

/* NOTE: Consider using filterx_object_extract_bytes_ref() to also support message_value. */
const gchar *
filterx_bytes_get_value_ref(FilterXObject *s, gsize *length)
{
  FilterXString *self = (FilterXString *) s;

  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(bytes)))
    return NULL;

  g_assert(length);
  *length = self->str_len;

  return self->str;
}

/* NOTE: Consider using filterx_object_extract_protobuf_ref() to also support message_value. */
const gchar *
filterx_protobuf_get_value_ref(FilterXObject *s, gsize *length)
{
  FilterXString *self = (FilterXString *) s;

  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(protobuf)))
    return NULL;

  g_assert(length);
  *length = self->str_len;

  return self->str;
}

static gboolean
_truthy(FilterXObject *s)
{
  FilterXString *self = (FilterXString *) s;
  return self->str_len > 0;
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXString *self = (FilterXString *) s;

  g_string_append_len(repr, self->str, self->str_len);
  *t = LM_VT_STRING;
  return TRUE;
}

static gboolean
_len(FilterXObject *s, guint64 *len)
{
  FilterXString *self = (FilterXString *) s;

  *len = self->str_len;
  return TRUE;
}

static void
_free(FilterXObject *s)
{
  FilterXString *self = (FilterXString *) s;
  if (self->super.flags & FILTERX_STRING_FLAG_STR_ALLOCATED)
    g_free((gchar *) self->str);
  else if (self->super.flags & FILTERX_STRING_FLAG_STR_BORROWED_SLICE)
    filterx_object_unref(self->storage.slice);
  filterx_object_free_method(s);
}

static gboolean
_string_repr(FilterXObject *s, GString *repr)
{
  FilterXString *self = (FilterXString *) s;
  repr = g_string_append_len(repr, self->str, self->str_len);
  return TRUE;
}

gboolean
string_format_json(const gchar *str, gsize str_len, gboolean json_escaping_needed, GString *json)
{
  g_string_append_c(json, '"');
  if (json_escaping_needed)
    append_unsafe_utf8_as_escaped(json, str, str_len, AUTF8_UNSAFE_QUOTE, "\\u%04x", "\\\\x%02x");
  else
    g_string_append_len(json, str, str_len);
  g_string_append_c(json, '"');
  return TRUE;
}

static gboolean
_string_format_json(FilterXObject *s, GString *json)
{
  FilterXString *self = (FilterXString *) s;
  gsize orig_len = json->len;
  const gint quote_characters_len = 2;

  gboolean success = string_format_json(self->str, self->str_len, filterx_string_is_json_escaping_needed(s), json);

  if ((self->str_len + quote_characters_len) == (json->len - orig_len))
    {
      /* no escapes were necessary, we only enclosed the original value in
       * quotes: cache the result in our flags to avoid escaping the same
       * string the next time */

      filterx_string_mark_safe_without_json_escaping(s);
    }
  return success;
}

static FilterXObject *
_string_add(FilterXObject *s, FilterXObject *object)
{
  FilterXString *self = (FilterXString *) s;

  const gchar *other_str;
  gsize other_str_len;
  if (!filterx_object_extract_string_ref(object, &other_str, &other_str_len))
    {
      filterx_eval_push_error_info_printf("Failed to add object to string", NULL,
                                          "Right hand side must be string, got: %s",
                                          filterx_object_get_type_name(object));
      return NULL;
    }

  gsize buffer_len = self->str_len + other_str_len;
  gchar *buffer = g_malloc(buffer_len + 1);
  memcpy(buffer, self->str, self->str_len);
  memcpy(buffer + self->str_len, other_str, other_str_len);
  buffer[buffer_len] = 0;
  FilterXObject *result = filterx_string_new_take(buffer, buffer_len);
  return result;
}

/* we support clone of stack allocated strings */
static FilterXObject *
_string_clone(FilterXObject *s)
{
  FilterXString *self = (FilterXString *) s;

  return filterx_string_new(self->str, self->str_len);
}

static inline FilterXString *
_string_new(const gchar *str, gssize str_len, FilterXStringTranslateFunc translate)
{
  if (str_len == -1)
    str_len = strlen(str);

  g_assert(str_len < G_MAXUINT);

  FilterXString *self = filterx_new_object_with_extra(FilterXString, str_len + 1);
  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(string));

  if (translate)
    translate(self->storage.bytes, str, (gsize *) &str_len);
  else
    memcpy(self->storage.bytes, str, str_len);
  self->str_len = str_len;

  /* NOTE: in DEBUG mode we always use a non-NUL termination to trigger
   * length handling bugs in newly introduced code */

  self->str = self->storage.bytes;
#if SYSLOG_NG_ENABLE_DEBUG
  self->storage.bytes[str_len] = '`';
#else
  self->storage.bytes[str_len] = 0;
#endif

  return self;
}

static void
_string_freeze(FilterXObject **pself, FilterXObjectFreezer *freezer)
{
  FilterXString *self = (FilterXString *) *pself;

  gchar *dedup_key = g_strdup_printf("string_%.*s", self->str_len, self->str);
  if (!filterx_object_freezer_dedup(freezer, pself, dedup_key))
    {
      _filterx_string_hash(self);
      if (unsafe_utf8_is_escaping_needed(self->str, self->str_len, AUTF8_UNSAFE_QUOTE))
        filterx_string_mark_safe_without_json_escaping(&self->super);

      filterx_object_freezer_add(freezer, dedup_key, *pself);
    }
}

guint
_filterx_string_hash(FilterXString *self)
{
  /* NOTE: this is a data race, as self->hash is written without atomics and
   * also without mutexes.  Since self->hash is aligned, and is just 32
   * bits, torn writes do not happen in modern architectures (x86, x86_64,
   * arm).  */

  G_STATIC_ASSERT(sizeof(self->hash) == sizeof(guint32));
  self->hash = strn_hash(self->str, self->str_len);
  return self->hash;
}

FilterXObject *
_filterx_string_new(const gchar *str, gssize str_len)
{
  return &_string_new(str, str_len, NULL)->super;
}

FilterXObject *
filterx_string_new_translated(const gchar *str, gssize str_len, FilterXStringTranslateFunc translate)
{
  return &_string_new(str, str_len, translate)->super;
}

FilterXObject *
filterx_string_new_take(gchar *str, gssize str_len)
{
  g_assert(str_len != -1);
  FilterXString *self = filterx_new_object(FilterXString);
  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(string));

  self->str_len = str_len;
  self->str = str;
  self->super.flags |= FILTERX_STRING_FLAG_STR_ALLOCATED;

  return &self->super;
}

static void
_deescape_json_string(gchar *target, const gchar *source, gsize *source_len)
{
  const gchar *input_pos = source;
  gchar *output_pos = target;
  gsize len = *source_len;
  const gchar *backslash;
  gssize remaining_len = len;

  backslash = memchr(input_pos, '\\', len);
  while (backslash != NULL)
    {
      gssize segment_len = backslash - input_pos;

      /* copy characters up to the backslash */
      memcpy(output_pos, input_pos, segment_len);
      output_pos += segment_len;
      input_pos = backslash + 1;
      remaining_len = len - (input_pos - source);

      if (remaining_len <= 0)
        break;

      /* translate escaped character */
      if (*input_pos == 'u')
        {
          glong unicode_codepoint;

          input_pos++;
          remaining_len--;

          if (scan_hex_int(&input_pos, (gsize *) &remaining_len, 4, &unicode_codepoint))
            output_pos += g_unichar_to_utf8((gunichar) unicode_codepoint, output_pos);
        }
      else
        {
          gchar escaped_character = 0;

          /* control character */
          switch (*input_pos)
            {
            case '/':
              escaped_character = '/';
              break;
            case 'b':
              escaped_character = '\b';
              break;
            case 'f':
              escaped_character = '\f';
              break;
            case 'n':
              escaped_character = '\n';
              break;
            case 'r':
              escaped_character = '\r';
              break;
            case 't':
              escaped_character = '\t';
              break;
            case '\\':
              escaped_character = '\\';
              break;
            case '"':
              escaped_character = '"';
              break;
            default:
              continue;
            }
          *output_pos = escaped_character;
          output_pos++;
          input_pos++;
          remaining_len--;
        }
      backslash = remaining_len > 0 ? memchr(input_pos, '\\', remaining_len) : NULL;
    }
  if (remaining_len > 0)
    {
      memcpy(output_pos, input_pos, remaining_len);
      output_pos += remaining_len;
    }
  *source_len = output_pos - target;
}

FilterXObject *
filterx_string_new_from_json_literal(const gchar *str, gssize str_len)
{
  return &_string_new(str, str_len, _deescape_json_string)->super;
}

FilterXObject *
filterx_string_new_slice(FilterXObject *object, gsize start, gsize end)
{
  FilterXString *self = filterx_new_object(FilterXString);
  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(string));

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(string)))
    {
      FilterXString *string = (FilterXString *) object;
      g_assert(end <= string->str_len && start <= end);
      self->str = &string->str[start];
    }
  else if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(message_value)))
    {
      FilterXMessageValue *mv = (FilterXMessageValue *) object;
      g_assert(end <= mv->repr_len && start <= end);
      self->str = &mv->repr[start];
    }
  else
    {
      return NULL;
    }

  self->super.flags |= FILTERX_STRING_FLAG_STR_BORROWED_SLICE;
  self->str_len = end - start;
  self->storage.slice = filterx_object_ref(object);
  return &self->super;
}

FilterXObject *
filterx_string_new_frozen(const gchar *str)
{
  FilterXObject *self = filterx_string_new(str, -1);
  filterx_eval_freeze_object(&self);
  return self;
}

static inline gsize
_get_base64_encoded_size(gsize len)
{
  return (len / 3 + 1) * 4 + 4;
}

gboolean
bytes_format_json(const gchar *str, gsize str_len, GString *json)
{
  g_string_append_c(json, '"');

  gint encode_state = 0;
  gint encode_save = 0;
  gsize init_len = json->len;

  /* expand the buffer and add space for the base64 encoded string */
  g_string_set_size(json, init_len + _get_base64_encoded_size(str_len));
  gsize out_len = g_base64_encode_step((const guchar *) str, str_len, FALSE, json->str + init_len,
                                       &encode_state, &encode_save);
  g_string_set_size(json, init_len + out_len + _get_base64_encoded_size(0));

#if !GLIB_CHECK_VERSION(2, 54, 0)
  /* See modules/basicfuncs/str-funcs.c: tf_base64encode() */
  if (((unsigned char *) &encode_save)[0] == 1)
    ((unsigned char *) &encode_save)[2] = 0;
#endif

  out_len += g_base64_encode_close(FALSE, json->str + init_len + out_len, &encode_state, &encode_save);
  g_string_set_size(json, init_len + out_len);

  g_string_append_c(json, '"');
  return TRUE;
}

static gboolean
_bytes_format_json(FilterXObject *s, GString *json)
{
  FilterXString *self = (FilterXString *) s;

  return bytes_format_json(self->str, self->str_len, json);
}

static gboolean
_bytes_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXString *self = (FilterXString *) s;

  g_string_append_len(repr, self->str, self->str_len);
  *t = LM_VT_BYTES;
  return TRUE;
}

static gboolean
_protobuf_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXString *self = (FilterXString *) s;

  g_string_append_len(repr, self->str, self->str_len);
  *t = LM_VT_PROTOBUF;
  return TRUE;
}

static gboolean
_bytes_repr(FilterXObject *s, GString *repr)
{
  FilterXString *self = (FilterXString *) s;

  gsize target_len = self->str_len * 2;
  gsize repr_len = repr->len;
  g_string_set_size(repr, target_len + repr_len);
  format_hex_string_with_delimiter(self->str, self->str_len, repr->str + repr_len, target_len + 1, 0);
  return TRUE;
}

static FilterXObject *
_bytes_add(FilterXObject *s, FilterXObject *object)
{
  FilterXString *self = (FilterXString *) s;

  const gchar *other_str;
  gsize other_str_len;
  if (!filterx_object_extract_bytes_ref(object, &other_str, &other_str_len))
    return NULL;

  gsize buffer_len = self->str_len + other_str_len;
  gchar *buffer = g_malloc(buffer_len);
  memcpy(buffer, self->str, self->str_len);
  memcpy(buffer + self->str_len, other_str, other_str_len);
  FilterXObject *result = filterx_bytes_new_take(buffer, buffer_len);
  return result;
}

FilterXObject *
filterx_bytes_new(const gchar *mem, gssize mem_len)
{
  g_assert(mem_len != -1);
  FilterXString *self = (FilterXString *) _string_new(mem, mem_len, NULL);
  self->super.type = &FILTERX_TYPE_NAME(bytes);
  return &self->super;
}

FilterXObject *
filterx_bytes_new_take(gchar *mem, gssize mem_len)
{
  FilterXObject *s = filterx_string_new_take(mem, mem_len);
  s->type = &FILTERX_TYPE_NAME(bytes);
  return s;
}

FilterXObject *
filterx_protobuf_new(const gchar *mem, gssize mem_len)
{
  g_assert(mem_len != -1);
  FilterXString *self = (FilterXString *) _string_new(mem, mem_len, NULL);
  self->super.type = &FILTERX_TYPE_NAME(protobuf);
  return &self->super;
}

FilterXObject *
filterx_typecast_string(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(string)))
    return filterx_object_ref(object);

  GString *buf = scratch_buffers_alloc();

  if (!filterx_object_str(object, buf))
    {
      filterx_eval_push_error_info_printf("Failed to typecast", s,
                                          "from_type: %s, to_type: string",
                                          filterx_object_get_type_name(object));
      return NULL;
    }

  return filterx_string_new(buf->str, buf->len);
}

FilterXObject *
filterx_typecast_bytes(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(bytes)))
    return filterx_object_ref(object);

  const gchar *data;
  gsize size;
  if (filterx_object_extract_string_ref(object, &data, &size) ||
      filterx_object_extract_protobuf_ref(object, &data, &size))
    {
      return filterx_bytes_new(data, size);
    }

  filterx_eval_push_error_info_printf("Failed to typecast", s,
                                      "from_type: %s, to_type: bytes",
                                      filterx_object_get_type_name(object));
  return NULL;
}

FilterXObject *
filterx_typecast_protobuf(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(protobuf)))
    {
      filterx_object_ref(object);
      return object;
    }

  const gchar *data;
  gsize size;
  if (filterx_object_extract_protobuf_ref(object, &data, &size) ||
      filterx_object_extract_bytes_ref(object, &data, &size))
    return filterx_protobuf_new(data, size);

  filterx_eval_push_error_info_printf("Failed to typecast", s,
                                      "from_type: %s, to_type: protobuf",
                                      filterx_object_get_type_name(object));

  return NULL;
}

/* these types are independent type-wise but share a lot of the details */

FILTERX_DEFINE_TYPE(string, FILTERX_TYPE_NAME(object),
                    .marshal = _marshal,
                    .len = _len,
                    .format_json = _string_format_json,
                    .truthy = _truthy,
                    .repr = _string_repr,
                    .add = _string_add,
                    .clone = _string_clone,
                    .freeze = _string_freeze,
                    .free_fn = _free,
                   );

FILTERX_DEFINE_TYPE(bytes, FILTERX_TYPE_NAME(object),
                    .marshal = _bytes_marshal,
                    .len = _len,
                    .format_json = _bytes_format_json,
                    .truthy = _truthy,
                    .repr = _bytes_repr,
                    .add = _bytes_add,
                    .free_fn = _free,
                   );

FILTERX_DEFINE_TYPE(protobuf, FILTERX_TYPE_NAME(object),
                    .len = _len,
                    .marshal = _protobuf_marshal,
                    .format_json = _bytes_format_json,
                    .truthy = _truthy,
                    .repr = _bytes_repr,
                    .free_fn = _free,
                   );

void
filterx_string_global_init(void)
{
  fx_string_cache[FILTERX_STRING_ZERO_LENGTH] = &_string_new("", 0, NULL)->super;
  filterx_object_hibernate(fx_string_cache[FILTERX_STRING_ZERO_LENGTH]);

  for (gint i = 0; i < 10; i++)
    {
      gchar number[2] = { i+'0', 0 };
      fx_string_cache[FILTERX_STRING_NUMBER0+i] = &_string_new(number, 1, NULL)->super;
      filterx_object_hibernate(fx_string_cache[FILTERX_STRING_NUMBER0+i]);
    }
}

void
filterx_string_global_deinit(void)
{
  for (gint i = 0; i < FILTERX_STRING_CACHE_SIZE; i++)
    {
      filterx_object_unhibernate_and_free(fx_string_cache[i]);
    }
}

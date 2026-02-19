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
#ifndef FILTERX_OBJECT_STRING_H_INCLUDED
#define FILTERX_OBJECT_STRING_H_INCLUDED

#include "filterx-object.h"
#include "str-utils.h"

/* storage for the string is allocated separately, needs an explicit free() */
#define FILTERX_STRING_FLAG_STR_ALLOCATED          0x01

/* the string holds a value that does not need escaping when generating a JSON string literal */
#define FILTERX_STRING_FLAG_NO_JSON_ESCAPE_NEEDED  0x02

/* cache indices */
enum
{
  FILTERX_STRING_ZERO_LENGTH,
  FILTERX_STRING_NUMBER0,
  FILTERX_STRING_NUMBER1,
  FILTERX_STRING_NUMBER2,
  FILTERX_STRING_NUMBER3,
  FILTERX_STRING_NUMBER4,
  FILTERX_STRING_NUMBER5,
  FILTERX_STRING_NUMBER6,
  FILTERX_STRING_NUMBER7,
  FILTERX_STRING_NUMBER8,
  FILTERX_STRING_NUMBER9,
  FILTERX_STRING_CACHE_SIZE,
};

extern FilterXObject *fx_string_cache[FILTERX_STRING_CACHE_SIZE];

typedef struct _FilterXString FilterXString;
struct _FilterXString
{
  FilterXObject super;
  const gchar *str;
  guint32 str_len;
  volatile guint32 hash;
  union
  {
    FilterXObject *slice;
    gchar bytes[0];
  } storage;
};


typedef void (*FilterXStringTranslateFunc)(gchar *target, const gchar *source, gsize *source_len);

FILTERX_DECLARE_TYPE(string);
FILTERX_DECLARE_TYPE(bytes);
FILTERX_DECLARE_TYPE(protobuf);

gboolean string_format_json(const gchar *str, gsize str_len, gboolean json_escaping_needed, GString *json);
gboolean bytes_format_json(const gchar *str, gsize str_len, GString *json);

const gchar *filterx_bytes_get_value_ref(FilterXObject *s, gsize *length);
const gchar *filterx_protobuf_get_value_ref(FilterXObject *s, gsize *length);
FilterXObject *filterx_typecast_string(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_typecast_bytes(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_typecast_protobuf(FilterXExpr *s, FilterXObject *args[], gsize args_len);

FilterXObject *filterx_string_new_translated(const gchar *str, gssize str_len, FilterXStringTranslateFunc translate);
FilterXObject *filterx_string_new_take(gchar *str, gssize str_len);
FilterXObject *filterx_string_new_from_json_literal(const gchar *str, gssize str_len);
FilterXObject *filterx_bytes_new(const gchar *str, gssize str_len);
FilterXObject *filterx_bytes_new_take(gchar *str, gssize str_len);
FilterXObject *filterx_protobuf_new(const gchar *str, gssize str_len);

/* NOTE: Consider using filterx_object_extract_string_ref() to also support message_value.
 */
static inline const gchar *
filterx_string_get_value_ref(FilterXObject *s, gsize *length)
{
  FilterXString *self = (FilterXString *) s;

  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(string)))
    return NULL;

  *length = self->str_len;
  return self->str;
}

/* get string value and assert if the terminating NUL is present, should
 * only be used in cases where we are sure that the FilterXString is a
 * literal string, which is always NUL terminated */
static inline const gchar *
filterx_string_get_value_ref_and_assert_nul(FilterXObject *s, gsize *length)
{
  gsize len = 0;
  const gchar *str = filterx_string_get_value_ref(s, &len);
  if (!str)
    return NULL;

#if SYSLOG_NG_ENABLE_DEBUG

  /* in DEBUG mode we are _never_ NUL terminating FilterXString instances to
   * trigger any issues that relate to length handling.  But we must use
   * this hack below to turn them into NUL terminated if the application
   * code correctly requests it using this function.
    */
  g_assert(str[len] == 0 || str[len] == '`');
  ((gchar *) str)[len] = 0;
#else
  g_assert(str[len] == 0);
#endif
  if (length)
    *length = len;
  return str;
}

#define filterx_string_get_value_as_cstr(s) \
  ({ \
    gsize __len; \
    const gchar *__str = filterx_string_get_value_ref(s, &__len); \
    if (__str) { APPEND_ZERO(__str, __str, __len); } \
    __str; \
  })

#define filterx_string_get_value_as_cstr_len(s, len) \
  ({ \
    const gchar *__str = filterx_string_get_value_ref(s, len); \
    if (__str) { APPEND_ZERO(__str, __str, *len); } \
    __str; \
  })

static inline gchar *
filterx_string_strdup_value(FilterXObject *s)
{
  gsize len;
  const gchar *str = filterx_string_get_value_ref(s, &len);
  if (!str)
    return NULL;
  return g_strndup(str, len);
}

static inline void
filterx_string_mark_safe_without_json_escaping(FilterXObject *s)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(filterx_object_is_type(s, &FILTERX_TYPE_NAME(string)));
#endif

  s->flags |= FILTERX_STRING_FLAG_NO_JSON_ESCAPE_NEEDED;
}

static inline gboolean
filterx_string_is_json_escaping_needed(FilterXObject *s)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(filterx_object_is_type(s, &FILTERX_TYPE_NAME(string)));
#endif

  if (s->flags & FILTERX_STRING_FLAG_NO_JSON_ESCAPE_NEEDED)
    return FALSE;
  return TRUE;
}

static inline FilterXObject *
_filterx_string_resolve_from_cache(const gchar *str, gssize str_len)
{
  if (str_len == 0 || str[0] == 0)
    {
      return filterx_object_ref(fx_string_cache[FILTERX_STRING_ZERO_LENGTH]);
    }
  else if (str[0] >= '0' && str[0] < '9' && (str_len == 1 || str[1] == 0))
    {
      gint index = str[0] - '0';
      return filterx_object_ref(fx_string_cache[FILTERX_STRING_NUMBER0 + index]);
    }
  return NULL;
}

/* slow path */
FilterXObject *_filterx_string_new(const gchar *str, gssize str_len);

static inline FilterXObject *
filterx_string_new(const gchar *str, gssize str_len)
{
  FilterXObject *cached = _filterx_string_resolve_from_cache(str, str_len);
  if (cached)
    return cached;
  return _filterx_string_new(str, str_len);
}

/* slow paths */
FilterXObject *_filterx_string_new_slice_from_borrowed_str_and_len(FilterXObject *object, const gchar *str, gsize str_len);
FilterXObject *_filterx_string_new_slice_from_non_string(FilterXObject *object, gsize start, gsize end);

static inline FilterXObject *
filterx_string_new_slice(FilterXObject *object, gsize start, gsize end)
{
  const gchar *str;
  gsize len;

  str = filterx_string_get_value_ref(object, &len);
  if (str)
    {
      g_assert(end <= len && start <= end);
      FilterXObject *cached = _filterx_string_resolve_from_cache(str, end - start);
      if (cached)
        return cached;
      return _filterx_string_new_slice_from_borrowed_str_and_len(object, &str[start], end - start);
    }

  return _filterx_string_new_slice_from_non_string(object, start, end);
}

static inline FilterXObject *
filterx_string_new_frozen(const gchar *str, GlobalConfig *cfg)
{
  FilterXObject *self = filterx_string_new(str, -1);
  filterx_object_freeze(&self, cfg);
  return self;
}

guint
_filterx_string_hash(FilterXString *self);

static inline guint
filterx_string_hash(FilterXObject *s)
{
  g_assert(filterx_object_is_type(s, &FILTERX_TYPE_NAME(string)));

  FilterXString *self = (FilterXString *) s;
  if (self->hash)
    return self->hash;

  /* although this is racy for parallel access on the same object, it's not
   * really a problem, as the hash algorithm should have the same result,
   * so worst case, we calculate the hash 2 times.
   */

  return _filterx_string_hash(self);
}

/* glib hash equal function */
static inline gboolean
filterx_string_equal(FilterXObject *s, FilterXObject *o)
{
  if (s == o)
    return TRUE;

  FilterXString *self = (FilterXString *) s;
  FilterXString *other = (FilterXString *) o;
  return strn_eq_strn(self->str, self->str_len, other->str, other->str_len);
}

void filterx_string_global_init(void);
void filterx_string_global_deinit(void);

#define FILTERX_STRING_STACK_INIT(cstr, cstr_len) \
  { \
    FILTERX_OBJECT_STACK_INIT(string), \
    .str = (cstr), \
    .str_len = (((gssize) cstr_len) == -1 ? (guint32) strlen(cstr) : (guint32) (cstr_len)), \
    .hash = 0, \
  }

#define FILTERX_STRING_DECLARE_ON_STACK(_name, cstr, cstr_len) \
  FilterXString __ ## _name ## storage = FILTERX_STRING_STACK_INIT(cstr, cstr_len); \
  FilterXObject *_name = &__ ## _name ## storage .super;

/* make sure we only call the unref if the pointer can potentially be
 * changed.  unchanged pointers don't need the unref call at all */

#define FILTERX_STRING_CLEAR_FROM_STACK(_name) \
  do { \
    if (_name != &__ ## _name ## storage.super) \
      filterx_object_unref(_name); \
  } while (0)


void filterx_string_global_init(void);
void filterx_string_global_deinit(void);

#endif

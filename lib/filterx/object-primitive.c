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
#include "filterx/object-primitive.h"
#include "filterx/filterx-grammar.h"
#include "filterx/filterx-globals.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "generic-number.h"
#include "str-format.h"
#include "plugin.h"
#include "cfg.h"
#include "str-utils.h"
#include "timeutils/misc.h"
#include "compat/json.h"
#include <math.h>

FilterXObject *fx_bool_cache[FILTERX_BOOL_CACHE_SIZE];
FilterXObject *fx_integer_cache[FILTERX_INTEGER_CACHE_SIZE];

static gboolean
_truthy(FilterXObject *s)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  return !gn_is_zero(&self->value);
}

static FilterXPrimitive *
filterx_primitive_new(FilterXType *type)
{
  FilterXPrimitive *self = g_new0(FilterXPrimitive, 1);

  filterx_object_init_instance(&self->super, type);
  return self;
}

static FilterXObject *
_integer_add(FilterXObject *s, FilterXObject *object)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  gint64 i;
  if (filterx_object_extract_integer(object, &i))
    return filterx_integer_new(gn_as_int64(&self->value) + i);

  gdouble d;
  if (filterx_object_extract_double(object, &d))
    return filterx_double_new(gn_as_int64(&self->value) + d);

  return NULL;
}

static FilterXObject *
_integer_clone(FilterXObject *s)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;
  return filterx_integer_new(gn_as_int64(&self->value));
}

gboolean
integer_repr(gint64 val, GString *repr)
{
  format_int64_padded(repr, 0, 0, 10, val);
  return TRUE;
}

static gboolean
_integer_format_json(FilterXObject *s, GString *json)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  return integer_repr(gn_as_int64(&self->value), json);
}

static gboolean
_integer_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;
  *t = LM_VT_INTEGER;
  return integer_repr(gn_as_int64(&self->value), repr);
}

static inline FilterXObject *
_integer_wrap(gint64 value)
{
  FilterXPrimitive *self = filterx_primitive_new(&FILTERX_TYPE_NAME(integer));
  gn_set_int64(&self->value, value);
  return &self->super;
}

FilterXObject *
_filterx_integer_new(gint64 value)
{
  return _integer_wrap(value);
}

static FilterXObject *
_double_add(FilterXObject *s, FilterXObject *object)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  gint64 i;
  if (filterx_object_extract_integer(object, &i))
    return filterx_double_new(gn_as_double(&self->value) + i);

  gdouble d;
  if (filterx_object_extract_double(object, &d))
    return filterx_double_new(gn_as_double(&self->value) + d);

  return NULL;
}

gboolean
double_repr(double dbl, GString *repr)
{
  if (isnan(dbl))
    {
      g_string_append_len(repr, "NaN", 3);
    }
  else if (isinf(dbl))
    {
      if (dbl < 0)
        g_string_append_c(repr, '-');
      g_string_append_len(repr, "Infinity", 8);
    }
  else
    {
      gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

      g_ascii_dtostr(buf, sizeof(buf), dbl);
      gchar *dot = strchr(buf, '.');
      if (!dot && strchr(buf, 'e') == NULL)
        {
          /* no fractions, not scientific notation */
          strcat(buf, ".0");
        }
      else if (dot)
        {
          gchar *last_useful_digit = NULL;
          for (gchar *c = dot+1; *c; c++)
            {
              if (*c != '0')
                last_useful_digit = c;
            }
          /* truncate trailing zeroes */
          if (last_useful_digit)
            *(last_useful_digit+1) = 0;
        }
      g_string_append(repr, buf);
    }
  return TRUE;
}

static gboolean
_double_format_json(FilterXObject *s, GString *json)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  return double_repr(gn_as_double(&self->value), json);
}

static gboolean
_double_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;
  *t = LM_VT_DOUBLE;
  return double_repr(gn_as_double(&self->value), repr);
}

FilterXObject *
filterx_double_new(gdouble value)
{
  FilterXPrimitive *self = filterx_primitive_new(&FILTERX_TYPE_NAME(double));
  gn_set_double(&self->value, value, -1);
  return &self->super;
}

gboolean
bool_repr(gboolean bool_val, GString *repr)
{
  if (bool_val)
    g_string_append(repr, "true");
  else
    g_string_append(repr, "false");
  return TRUE;
}

gboolean
bool_format_json(gboolean bool_val, GString *json)
{
  return bool_repr(bool_val, json);
}

static gboolean
_bool_format_json(FilterXObject *s, GString *json)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  return bool_repr(!gn_is_zero(&self->value), json);
}

static gboolean
_bool_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;
  *t = LM_VT_BOOLEAN;
  return bool_repr(!gn_is_zero(&self->value), repr);
}

FilterXObject *
_bool_wrap(gboolean value)
{
  FilterXPrimitive *self = filterx_primitive_new(&FILTERX_TYPE_NAME(boolean));
  gn_set_int64(&self->value, (gint64) value);
  return &self->super;
}

static gboolean
_lookup_enum_value_from_array(const FilterXEnumDefinition *enum_defs, const gchar *enum_name, gint64 *value)
{
  gint64 i = 0;
  while (TRUE)
    {
      const FilterXEnumDefinition *enum_def = &enum_defs[i];

      if (!enum_def->name)
        return FALSE;

      if (strcasecmp(enum_def->name, enum_name) != 0)
        {
          i++;
          continue;
        }

      *value = enum_def->value;
      return TRUE;
    }
}

static gboolean
_lookup_enum_value(GlobalConfig *cfg, const gchar *namespace_name, const gchar *enum_name, gint64 *value)
{
  Plugin *p = cfg_find_plugin(cfg, LL_CONTEXT_FILTERX_ENUM, namespace_name);

  if (p == NULL)
    return FALSE;

  const FilterXEnumDefinition *enum_defs = plugin_construct(p);
  if (enum_defs == NULL)
    return FALSE;

  return _lookup_enum_value_from_array(enum_defs, enum_name, value);
}

FilterXObject *
filterx_enum_new(GlobalConfig *cfg, const gchar *namespace_name, const gchar *enum_name)
{
  FilterXPrimitive *self = filterx_primitive_new(&FILTERX_TYPE_NAME(integer));

  gint64 value;
  if (!_lookup_enum_value(cfg, namespace_name, enum_name, &value))
    {
      filterx_object_unref(&self->super);
      return NULL;
    }

  gn_set_int64(&self->value, value);
  return &self->super;
}

/* NOTE: Consider using filterx_object_extract_generic_number() to also support message_value. */
GenericNumber
filterx_primitive_get_value(FilterXObject *s)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;
  return self->value;
}

FilterXObject *
filterx_typecast_boolean(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(boolean)))
    {
      filterx_object_ref(object);
      return object;
    }

  return filterx_boolean_new(filterx_object_truthy(object));
}

FilterXObject *
filterx_typecast_integer(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(integer)))
    return filterx_object_ref(object);

  GenericNumber gn;
  if (filterx_object_extract_generic_number(object, &gn))
    return filterx_integer_new(gn_as_int64(&gn));

  const gchar *str;
  gsize str_len;
  if (filterx_object_extract_string_ref(object, &str, &str_len))
    {
      APPEND_ZERO(str, str, str_len);

      gchar *endptr;
      gint64 val = g_ascii_strtoll(str, &endptr, 10);
      if (str != endptr && *endptr == '\0')
        return filterx_integer_new(val);
    }

  UnixTime ut;
  if (filterx_object_extract_datetime(object, &ut))
    return filterx_integer_new(ut.ut_sec * USEC_PER_SEC + ut.ut_usec);

  filterx_eval_push_error_info_printf("Failed to typecast", s,
                                      "from_type: %s, to_type: integer",
                                      filterx_object_get_type_name(object));
  return NULL;
}

FilterXObject *
filterx_typecast_double(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *object = filterx_typecast_get_arg(s, args, args_len);
  if (!object)
    return NULL;

  if (filterx_object_is_type(object, &FILTERX_TYPE_NAME(double)))
    return filterx_object_ref(object);

  GenericNumber gn;
  if (filterx_object_extract_generic_number(object, &gn))
    return filterx_double_new(gn_as_double(&gn));

  const gchar *str;
  gsize str_len;
  if (filterx_object_extract_string_ref(object, &str, &str_len))
    {
      APPEND_ZERO(str, str, str_len);

      gchar *endptr;
      gdouble val = g_ascii_strtod(str, &endptr);
      if (str != endptr && *endptr == '\0')
        return filterx_double_new(val);
    }

  UnixTime ut;
  if (filterx_object_extract_datetime(object, &ut))
    return filterx_double_new(ut.ut_sec + (gdouble) ut.ut_usec / USEC_PER_SEC);

  filterx_eval_push_error_info_printf("Failed to typecast", s,
                                      "from_type: %s, to_type: double",
                                      filterx_object_get_type_name(object));
  return NULL;
}

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  LogMessageValueType t;
  return filterx_object_marshal_append(s, repr, &t);
}

FILTERX_DEFINE_TYPE(primitive, FILTERX_TYPE_NAME(object),
                    .truthy = _truthy,
                    .repr = _repr,
                   );

FILTERX_DEFINE_TYPE(integer, FILTERX_TYPE_NAME(primitive),
                    .marshal = _integer_marshal,
                    .format_json = _integer_format_json,
                    .add = _integer_add,
                    .clone = _integer_clone,
                   );

FILTERX_DEFINE_TYPE(double, FILTERX_TYPE_NAME(primitive),
                    .marshal = _double_marshal,
                    .format_json = _double_format_json,
                    .add = _double_add,
                   );

FILTERX_DEFINE_TYPE(boolean, FILTERX_TYPE_NAME(primitive),
                    .marshal = _bool_marshal,
                    .format_json = _bool_format_json,
                   );

void
filterx_primitive_global_init(void)
{
  fx_bool_cache[FALSE] = _bool_wrap(FALSE);
  filterx_object_hibernate(fx_bool_cache[FALSE]);

  fx_bool_cache[TRUE] = _bool_wrap(TRUE);
  filterx_object_hibernate(fx_bool_cache[TRUE]);

  for (gint64 i = FILTERX_INTEGER_CACHE_MIN; i <= FILTERX_INTEGER_CACHE_MAX; i++)
    {
      fx_integer_cache[FILTERX_INTEGER_CACHE_IDX(i)] = _integer_wrap(i);
      filterx_object_hibernate(fx_integer_cache[FILTERX_INTEGER_CACHE_IDX(i)]);
    }
}

void
filterx_primitive_global_deinit(void)
{
  filterx_object_unhibernate_and_free(fx_bool_cache[FALSE]);
  filterx_object_unhibernate_and_free(fx_bool_cache[TRUE]);

  for (gint64 i = FILTERX_INTEGER_CACHE_MIN; i <= FILTERX_INTEGER_CACHE_MAX; i++)
    filterx_object_unhibernate_and_free(fx_integer_cache[FILTERX_INTEGER_CACHE_IDX(i)]);
}

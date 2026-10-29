/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx-func-geoip.h"
#include "maxminddb-helper.h"

#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/filterx-sequence.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-extractor.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct _FilterXFunctionGeoIP
{
  FilterXFunction super;
  FilterXExpr *ip;
  /* NOTE: This needs no locking: libmaxminddb lookups never mutate the handle */
  MMDB_s *database;
  gchar *database_path;
  gchar **entry_path;
} FilterXFunctionGeoIP;

static FilterXObject *_entry_data_list_to_filterx(MMDB_entry_data_list_s **entry_data_list, gint *status);

/* FilterX integers are signed 64 bit; anything larger is returned as its
 * exact decimal representation in a string rather than wrapping around. */
static FilterXObject *
_uint64_to_filterx(guint64 value)
{
  if (value <= (guint64) G_MAXINT64)
    return filterx_integer_new((gint64) value);

  return filterx_string_new_take(g_strdup_printf("%" G_GUINT64_FORMAT, value), -1);
}

/* 128 bit values are rendered as a 0x-prefixed, zero padded hex string, the
 * same representation MMDB_dump_entry_data_list() uses. */
static FilterXObject *
_uint128_to_filterx(MMDB_entry_data_s *entry_data)
{
  gchar buf[2 + 32 + 1];

#if MMDB_UINT128_IS_BYTE_ARRAY
  gsize pos = 0;

  buf[pos++] = '0';
  buf[pos++] = 'x';
  for (gsize i = 0; i < 16; i++)
    pos += g_snprintf(buf + pos, sizeof(buf) - pos, "%02x", entry_data->uint128[i]);
#else
  guint64 hi = (guint64) (entry_data->uint128 >> 64);
  guint64 lo = (guint64) entry_data->uint128;

  g_snprintf(buf, sizeof(buf), "0x%016" G_GINT64_MODIFIER "x%016" G_GINT64_MODIFIER "x", hi, lo);
#endif

  return filterx_string_new(buf, -1);
}

static FilterXObject *
_scalar_to_filterx(MMDB_entry_data_s *entry_data, gint *status)
{
  *status = MMDB_SUCCESS;

  switch (entry_data->type)
    {
    case MMDB_DATA_TYPE_UTF8_STRING:
      return filterx_string_new(entry_data->utf8_string, entry_data->data_size);
    case MMDB_DATA_TYPE_BYTES:
      return filterx_bytes_new((const gchar *) entry_data->bytes, entry_data->data_size);
    case MMDB_DATA_TYPE_DOUBLE:
      return filterx_double_new(entry_data->double_value);
    case MMDB_DATA_TYPE_FLOAT:
      return filterx_double_new((gdouble) entry_data->float_value);
    case MMDB_DATA_TYPE_UINT16:
      return filterx_integer_new(entry_data->uint16);
    case MMDB_DATA_TYPE_UINT32:
      return filterx_integer_new(entry_data->uint32);
    case MMDB_DATA_TYPE_INT32:
      return filterx_integer_new(entry_data->int32);
    case MMDB_DATA_TYPE_UINT64:
      return _uint64_to_filterx(entry_data->uint64);
    case MMDB_DATA_TYPE_UINT128:
      return _uint128_to_filterx(entry_data);
    case MMDB_DATA_TYPE_BOOLEAN:
      return filterx_boolean_new(entry_data->boolean);
    default:
      /* pointers, containers and end markers never appear in a decoded
       * entry, anything else means the data section is corrupt */
      *status = MMDB_INVALID_DATA_ERROR;
      return NULL;
    }
}

static FilterXObject *
_map_to_filterx(MMDB_entry_data_list_s **entry_data_list, gint *status)
{
  guint32 size = (*entry_data_list)->entry_data.data_size;
  FilterXObject *dict = filterx_dict_new();
  filterx_object_cow_prepare(&dict);

  *entry_data_list = (*entry_data_list)->next;
  for (; size && *entry_data_list; size--)
    {
      if (MMDB_DATA_TYPE_UTF8_STRING != (*entry_data_list)->entry_data.type)
        {
          *status = MMDB_INVALID_DATA_ERROR;
          goto error;
        }

      const gchar *key = (*entry_data_list)->entry_data.utf8_string;
      gsize key_len = (*entry_data_list)->entry_data.data_size;
      *entry_data_list = (*entry_data_list)->next;

      FilterXObject *value = _entry_data_list_to_filterx(entry_data_list, status);
      if (MMDB_SUCCESS != *status)
        goto error;

      FILTERX_STRING_DECLARE_ON_STACK(dict_key, key, key_len);
      gboolean ok = filterx_object_set_subscript(dict, dict_key, &value);
      FILTERX_STRING_CLEAR_FROM_STACK(dict_key);
      filterx_object_unref(value);
      if (!ok)
        {
          *status = MMDB_INVALID_DATA_ERROR;
          goto error;
        }
    }

  /* the entry list ended before the map reached its declared size */
  if (size != 0)
    {
      *status = MMDB_INVALID_DATA_ERROR;
      goto error;
    }

  filterx_object_set_dirty(dict, FALSE);
  return dict;

error:
  filterx_object_unref(dict);
  return NULL;
}

static FilterXObject *
_array_to_filterx(MMDB_entry_data_list_s **entry_data_list, gint *status)
{
  guint32 size = (*entry_data_list)->entry_data.data_size;
  FilterXObject *list = filterx_list_new();
  filterx_object_cow_prepare(&list);

  *entry_data_list = (*entry_data_list)->next;
  for (; size && *entry_data_list; size--)
    {
      FilterXObject *value = _entry_data_list_to_filterx(entry_data_list, status);
      if (MMDB_SUCCESS != *status)
        goto error;

      gboolean ok = filterx_sequence_append(list, &value);
      filterx_object_unref(value);
      if (!ok)
        {
          *status = MMDB_INVALID_DATA_ERROR;
          goto error;
        }
    }

  /* the entry list ended before the array reached its declared size */
  if (size != 0)
    {
      *status = MMDB_INVALID_DATA_ERROR;
      goto error;
    }

  filterx_object_set_dirty(list, FALSE);
  return list;

error:
  filterx_object_unref(list);
  return NULL;
}

static FilterXObject *
_entry_data_list_to_filterx(MMDB_entry_data_list_s **entry_data_list, gint *status)
{
  switch ((*entry_data_list)->entry_data.type)
    {
    case MMDB_DATA_TYPE_MAP:
      return _map_to_filterx(entry_data_list, status);
    case MMDB_DATA_TYPE_ARRAY:
      return _array_to_filterx(entry_data_list, status);
    default:
      break;
    }

  FilterXObject *result = _scalar_to_filterx(&(*entry_data_list)->entry_data, status);
  *entry_data_list = (*entry_data_list)->next;
  return result;
}

typedef enum
{
  GEOIP_LOOKUP_FOUND,
  GEOIP_LOOKUP_NOT_FOUND,
  GEOIP_LOOKUP_ERROR,
} GeoIPLookupResult;

static GeoIPLookupResult
_lookup_entry_path(FilterXFunctionGeoIP *self, MMDB_entry_s *start, FilterXObject **out, gint *mmdb_status)
{
  MMDB_entry_data_s entry_data;

  *mmdb_status = MMDB_aget_value(start, &entry_data, (const char *const *const) self->entry_path);
  if (*mmdb_status != MMDB_SUCCESS)
    return (*mmdb_status == MMDB_LOOKUP_PATH_DOES_NOT_MATCH_DATA_ERROR)
           ? GEOIP_LOOKUP_NOT_FOUND : GEOIP_LOOKUP_ERROR;

  if (!entry_data.has_data)
    return GEOIP_LOOKUP_NOT_FOUND;

  if (entry_data.type != MMDB_DATA_TYPE_MAP && entry_data.type != MMDB_DATA_TYPE_ARRAY)
    {
      *out = _scalar_to_filterx(&entry_data, mmdb_status);
      return (*mmdb_status == MMDB_SUCCESS) ? GEOIP_LOOKUP_FOUND : GEOIP_LOOKUP_ERROR;
    }

  MMDB_entry_s sub_entry = { .mmdb = start->mmdb, .offset = entry_data.offset };
  MMDB_entry_data_list_s *entry_data_list = NULL;
  *mmdb_status = MMDB_get_entry_data_list(&sub_entry, &entry_data_list);
  if (*mmdb_status != MMDB_SUCCESS)
    return GEOIP_LOOKUP_ERROR;

  MMDB_entry_data_list_s *cursor = entry_data_list;
  *out = _entry_data_list_to_filterx(&cursor, mmdb_status);
  MMDB_free_entry_data_list(entry_data_list);

  return (*mmdb_status == MMDB_SUCCESS) ? GEOIP_LOOKUP_FOUND : GEOIP_LOOKUP_ERROR;
}

/* MMDB_lookup_string() goes through getaddrinfo(), which is needlessly
 * heavy for a per-message hot path even with AI_NUMERICHOST.  We only
 * accept numeric addresses anyway, so parse them with inet_pton() and use
 * MMDB_lookup_sockaddr() directly. */
static gboolean
_parse_ip(const gchar *ip_str, struct sockaddr_storage *addr)
{
  memset(addr, 0, sizeof(*addr));

  struct sockaddr_in *sin = (struct sockaddr_in *) addr;
  if (inet_pton(AF_INET, ip_str, &sin->sin_addr) == 1)
    {
      sin->sin_family = AF_INET;
      return TRUE;
    }

  struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) addr;
  if (inet_pton(AF_INET6, ip_str, &sin6->sin6_addr) == 1)
    {
      sin6->sin6_family = AF_INET6;
      return TRUE;
    }

  return FALSE;
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionGeoIP *self = (FilterXFunctionGeoIP *) s;

  if (!self->database)
    return filterx_null_new();

  FilterXObject *ip_obj = filterx_expr_eval(self->ip);
  if (!ip_obj)
    return NULL;

  FilterXObject *result = NULL;
  const gchar *ip_str;
  gsize ip_len;

  if (!filterx_object_extract_string_as_cstr_len(ip_obj, &ip_str, &ip_len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate geoip()",
                                          "ip argument must be string, got: %s",
                                          filterx_object_get_type_name(ip_obj));
      goto exit;
    }

  struct sockaddr_storage addr;
  if (!_parse_ip(ip_str, &addr))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate geoip()",
                                          "ip argument is not a valid IPv4 or IPv6 address: %s", ip_str);
      goto exit;
    }

  int mmdb_error;
  MMDB_lookup_result_s lookup_result = MMDB_lookup_sockaddr(self->database, (struct sockaddr *) &addr, &mmdb_error);

  if (mmdb_error != MMDB_SUCCESS)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate geoip()",
                                          "maxminddb lookup error for %s: %s", ip_str, MMDB_strerror(mmdb_error));
      goto exit;
    }

  if (!lookup_result.found_entry)
    {
      result = filterx_null_new();
      goto exit;
    }

  gint status;
  GeoIPLookupResult lookup_status = _lookup_entry_path(self, &lookup_result.entry, &result, &status);
  switch (lookup_status)
    {
    case GEOIP_LOOKUP_FOUND:
      break;
    case GEOIP_LOOKUP_NOT_FOUND:
      result = filterx_null_new();
      break;
    case GEOIP_LOOKUP_ERROR:
      filterx_eval_push_error_info_printf("Failed to evaluate geoip()",
                                          "maxminddb error while decoding the entry for %s: %s",
                                          ip_str, MMDB_strerror(status));
      break;
    default:
      g_assert_not_reached();
    }

exit:
  filterx_object_unref(ip_obj);
  return result;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionGeoIP *self = (FilterXFunctionGeoIP *) s;

  filterx_expr_unref(self->ip);
  g_free(self->database_path);
  g_strfreev(self->entry_path);
  if (self->database)
    {
      MMDB_close(self->database);
      g_free(self->database);
    }

  filterx_function_free_method(&self->super);
}

static gboolean
_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionGeoIP *self = (FilterXFunctionGeoIP *) s;

  return filterx_expr_visit(s, &self->ip, f, user_data);
}

static FilterXExpr *
_extract_ip_arg(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *ip_expr = filterx_function_args_get_expr(args, 0);
  if (!ip_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: ip. " FILTERX_FUNC_GEOIP_USAGE);
      return NULL;
    }

  return ip_expr;
}

static gboolean
_find_database_by_kind(FilterXFunctionGeoIP *self, const gchar *kind, GError **error)
{
  GError *kind_error = NULL;

  if (!mmdb_resolve_database_kind(kind, &self->database_path, &kind_error))
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "%s; to use a specific file, set the database argument", kind_error->message);
      g_error_free(kind_error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
_extract_optional_args(FilterXFunctionGeoIP *self, FilterXFunctionArgs *args, GError **error)
{
  gboolean exists;
  gsize len;

  const gchar *database = filterx_function_args_get_named_literal_string(args, "database", &len, &exists);
  if (exists && !database)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "database argument must be a string literal. " FILTERX_FUNC_GEOIP_USAGE);
      return FALSE;
    }

  gboolean kind_exists;
  const gchar *kind = filterx_function_args_get_named_literal_string(args, "kind", &len, &kind_exists);
  if (kind_exists && !kind)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "kind argument must be a string literal. " FILTERX_FUNC_GEOIP_USAGE);
      return FALSE;
    }

  if (database && kind)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "database and kind arguments are mutually exclusive. " FILTERX_FUNC_GEOIP_USAGE);
      return FALSE;
    }

  if (database)
    self->database_path = g_strdup(database);
  else if (!_find_database_by_kind(self, kind ? : "country", error))
    return FALSE;

  const gchar *field = filterx_function_args_get_named_literal_string(args, "field", &len, &exists);
  if (exists && !field)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "field argument must be a string literal. " FILTERX_FUNC_GEOIP_USAGE);
      return FALSE;
    }

  self->entry_path = g_strsplit(exists ? field : "country.iso_code", ".", -1);

  return TRUE;
}

static gboolean
_extract_args(FilterXFunctionGeoIP *self, FilterXFunctionArgs *args, GError **error)
{
  gsize args_len = filterx_function_args_len(args);
  if (args_len != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_GEOIP_USAGE);
      return FALSE;
    }

  self->ip = _extract_ip_arg(args, error);
  if (!self->ip)
    return FALSE;

  return _extract_optional_args(self, args, error);
}

static gboolean
_open_database(FilterXFunctionGeoIP *self, GError **error)
{
  if (!self->database_path)
    return TRUE;

  self->database = g_new0(MMDB_s, 1);

  if (!mmdb_open_database(self->database_path, self->database))
    {
      g_free(self->database);
      self->database = NULL;
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "geoip(): could not open database %s", self->database_path);
      return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_function_geoip_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionGeoIP *self = g_new0(FilterXFunctionGeoIP, 1);

  filterx_function_init_instance(&self->super, "geoip", FXE_READ);
  self->super.super.eval = _eval;
  self->super.super.walk_children = _walk;
  self->super.super.free_fn = _free;

  if (!_extract_args(self, args, error) ||
      !filterx_function_args_check(args, error) ||
      !_open_database(self, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(geoip, filterx_function_geoip_new);

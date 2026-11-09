/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023-2024 László Várady
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
#include "stats/stats-prometheus.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster.h"
#include "stats/stats-counter.h"
#include "timeutils/unixtime.h"
#include "str-utils.h"
#include "scratch-buffers.h"

#include <string.h>


/* Exposition format:
 *
 * A label value can be any sequence of UTF-8 characters, but the
 * backslash (\), double-quote ("), and line feed (\n) characters have to be
 * escaped as \\, \", and \n, respectively.
 */
static void
_append_sanitized_label_value(GString *buf, const gchar *value, gssize value_len)
{
  if (value_len < 0)
    value_len = strlen(value);
  const gchar *value_end = value + value_len;
  const gchar *run_start = value;

  while (value < value_end)
    {
      guchar c = (guchar) *value;

      if (c >= 0x80)
        {
          gunichar uchar = g_utf8_get_char_validated(value, value_end - value);

          if (G_UNLIKELY(uchar == (gunichar) -1 || uchar == (gunichar) -2))
            {
              g_string_append_len(buf, run_start, value - run_start);
              /* double backslash to conform to the format */
              g_string_append_printf(buf, "\\\\x%02x", c);
              value++;
              run_start = value;
              continue;
            }

          value = g_utf8_next_char(value);
          continue;
        }

      const gchar *escaped = NULL;
      switch (c)
        {
        case '\\':
          escaped = "\\\\";
          break;
        case '"':
          escaped = "\\\"";
          break;
        case '\n':
          escaped = "\\n";
          break;
        default:
          break;
        }

      if (escaped)
        {
          g_string_append_len(buf, run_start, value - run_start);
          g_string_append(buf, escaped);
          run_start = value + 1;
        }

      value++;
    }

  g_string_append_len(buf, run_start, value_end - run_start);
}

static inline gboolean
_is_valid_name_char(gchar c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')  || (c == '_') || (c >= '0' && c <= '9');
}

static inline gboolean
_is_str_empty(const gchar *str)
{
  return !str || strcmp(str, "") == 0;
}

static void
_append_sanitized_name(GString *buf, const gchar *name, gssize len)
{
  if (len < 0)
    len = strlen(name);
  const gchar *name_end = name + len;

  const gchar *run_start = name;

  for (const gchar *c = name; c < name_end; ++c)
    {
      if (_is_valid_name_char(*c))
        continue;

      g_string_append_len(buf, run_start, c - run_start);
      if (*c == '.' || *c == '-')
        g_string_append_c(buf, '_');

      /* remove otherwise */
      run_start = c + 1;
    }

  g_string_append_len(buf, run_start, name_end - run_start);
}

static void
_append_formatted_value(GString *buf, StatsClusterUnit stored_unit, gsize stored_value)
{
  guint64 converted_int = stored_value;
  gdouble converted_double = stored_value;
  gchar double_buf[G_ASCII_DTOSTR_BUF_SIZE];

  switch (stored_unit)
    {
    case SCU_GIB:
      converted_int *= 1024;
    case SCU_MIB:
      converted_int *= 1024;
    case SCU_KIB:
      converted_int *= 1024;
      g_string_append_printf(buf, "%"G_GUINT64_FORMAT, converted_int);
      break;

    case SCU_HOURS:
      converted_int *= 60;
    case SCU_MINUTES:
      converted_int *= 60;
    case SCU_SECONDS:
      g_string_append_printf(buf, "%"G_GUINT64_FORMAT, converted_int);
      break;

    case SCU_NANOSECONDS:
      converted_double /= 1e9;
      g_string_append(buf, g_ascii_dtostr(double_buf, G_N_ELEMENTS(double_buf), converted_double));
      break;

    case SCU_MILLISECONDS:
      converted_double /= 1e3;
      g_string_append(buf, g_ascii_dtostr(double_buf, G_N_ELEMENTS(double_buf), converted_double));
      break;

    default:
      /* no conversion */
      g_string_append_printf(buf, "%"G_GSIZE_FORMAT, stored_value);
      break;
    }
}

gchar *
stats_format_prometheus_format_value(StatsClusterUnit stored_unit, gsize stored_value)
{
  GString *value = scratch_buffers_alloc();

  _append_formatted_value(value, stored_unit, stored_value);
  return value->str;
}

static void
_append_counter_value(GString *buf, StatsCluster *sc, gint type)
{
  StatsClusterUnit stored_unit;

  stats_cluster_get_type_formatting(sc, type, &stored_unit);
  _append_formatted_value(buf, stored_unit, stats_cluster_get_type_value(sc, type));
}

static inline void
_append_formatted_label(GString *serialized_labels, const StatsClusterLabel *label, gboolean *comma_needed)
{
  /* NOTE: labels with a starting dot are excluded
   *
   * This is useful when the legacy instance_id and the prometheus labels
   * diverge.  In those cases the hidden label can be used as a legacy
   * label, as the instance_id formatting ignores the name.
   */
  if (label->name[0] == '.' || _is_str_empty(label->value))
    return;

  if (*comma_needed)
    g_string_append_c(serialized_labels, ',');
  else
    *comma_needed = TRUE;

  _append_sanitized_name(serialized_labels, label->name, label->name_len);
  g_string_append_len(serialized_labels, "=\"", 2);
  _append_sanitized_label_value(serialized_labels, label->value, label->value_len);
  g_string_append_c(serialized_labels, '"');
}

static inline gboolean
_is_timestamp(StatsCluster *sc, gint type)
{
  return strcmp(stats_cluster_get_type_name(sc, type), "stamp") == 0;
}

static void
_append_braced_labels(GString *buf, StatsClusterLabel *labels, gsize labels_len, const StatsClusterLabel *extra)
{
  gsize orig_size = buf->len;
  g_string_append_c(buf, '{');

  gboolean comma_needed = FALSE;
  for (gsize i = 0; i < labels_len; ++i)
    _append_formatted_label(buf, &labels[i], &comma_needed);

  if (extra)
    _append_formatted_label(buf, extra, &comma_needed);

  gboolean no_labels = buf->len == orig_size + 1;
  if (no_labels)
    g_string_truncate(buf, orig_size);
  else
    g_string_append_c(buf, '}');
}

static GString *
_format_legacy(StatsCluster *sc, gint type, StatsCounterItem *counter)
{
  GString *record = scratch_buffers_alloc();

  gchar component[64];

  g_string_append(record, METRIC_PREFIX);
  _append_sanitized_name(record, stats_cluster_get_component_name(sc, component, sizeof(component)), -1);

  gboolean global = !sc->key.legacy.component || sc->key.legacy.component == SCS_GLOBAL;
  if (global && !_is_str_empty(sc->key.legacy.id))
    {
      g_string_append_c(record, '_');
      _append_sanitized_name(record, sc->key.legacy.id, -1);
    }

  const gchar *type_name = stats_cluster_get_type_name(sc, type);
  if (g_strcmp0(type_name, "value") != 0)
    {
      g_string_append_c(record, '_');
      _append_sanitized_name(record, type_name, -1);
    }

  if (!global)
    {
      StatsClusterLabel labels[] =
      {
        stats_cluster_label("id", sc->key.legacy.id),
        stats_cluster_label("stat_instance", sc->key.legacy.instance),
      };
      _append_braced_labels(record, labels, G_N_ELEMENTS(labels), NULL);
    }

  g_string_append_c(record, ' ');
  _append_counter_value(record, sc, type);
  g_string_append_c(record, '\n');

  return record;
}

GString *
stats_prometheus_format_counter(StatsCluster *sc, gint type, StatsCounterItem *counter)
{
  if (_is_timestamp(sc, type))
    return NULL;

  if (!sc->key.name)
    return _format_legacy(sc, type, counter);

  GString *record = scratch_buffers_alloc();
  g_string_append(record, METRIC_PREFIX);
  _append_sanitized_name(record, sc->key.name, -1);

  const gchar *suffix = stats_cluster_get_type_name_suffix(sc, type);
  if (suffix)
    g_string_append(record, suffix);

  StatsClusterLabel type_label;
  gboolean needs_type_label = stats_cluster_get_type_label(sc, type, &type_label);
  _append_braced_labels(record, sc->key.labels, sc->key.labels_len, needs_type_label ? &type_label : NULL);

  g_string_append_c(record, ' ');
  _append_counter_value(record, sc, type);
  g_string_append_c(record, '\n');

  return record;
}

static void
stats_format_prometheus(StatsCluster *sc, gint type, StatsCounterItem *counter, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  StatsPrometheusRecordFunc process_record = (StatsPrometheusRecordFunc) args[0];
  gpointer process_record_arg = args[1];
  gboolean with_legacy = GPOINTER_TO_INT(args[2]);

  if (!sc->key.name && !with_legacy)
    return;

  ScratchBuffersMarker marker;
  scratch_buffers_mark(&marker);

  GString *record = stats_prometheus_format_counter(sc, type, counter);
  if (!record)
    return;

  process_record(record->str, process_record_arg);
  scratch_buffers_reclaim_marked(marker);
}

void
stats_prometheus_format_labels_append(StatsClusterLabel *labels, gsize labels_len, GString *buf)
{
  _append_braced_labels(buf, labels, labels_len, NULL);
}

void
stats_generate_prometheus(StatsPrometheusRecordFunc process_record, gpointer user_data, gboolean with_legacy,
                          gboolean *cancelled)
{
  gpointer format_prometheus_args[] = {process_record, user_data, GINT_TO_POINTER(with_legacy)};
  stats_lock();
  stats_foreach_counter(stats_format_prometheus, format_prometheus_args, cancelled);
  stats_unlock();
}

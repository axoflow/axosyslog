/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 Balazs Scheidler <bazsi@balabit.hu>
 * Copyright (c) 2022 Balazs Scheidler <bazsi77@gmail.com>
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

#include "multi-line/multi-line-factory.h"
#include "multi-line/regexp-multi-line.h"
#include "multi-line/indented-multi-line.h"
#include "multi-line/smart-multi-line.h"
#include "multi-line/timestamp-multi-line.h"
#include "messages.h"

#include <string.h>
#include <strings.h>

MultiLineLogic *
multi_line_factory_construct(const MultiLineOptions *options)
{
  switch (options->mode)
    {
    case MLM_INDENTED:
      return indented_multi_line_new();
    case MLM_REGEXP_PREFIX_GARBAGE:
      return regexp_multi_line_new(RML_PREFIX_GARBAGE, options->regexp.prefix, options->regexp.garbage);
    case MLM_REGEXP_PREFIX_SUFFIX:
      return regexp_multi_line_new(RML_PREFIX_SUFFIX, options->regexp.prefix, options->regexp.garbage);
    case MLM_SMART:
      return smart_multi_line_new();
    case MLM_TIMESTAMP:
      return timestamp_multi_line_new(options->timestamp.pairs);
    case MLM_NONE:
      return NULL;

    default:
      g_assert_not_reached();
      break;
    }
  g_assert_not_reached();
}

/* the modes that share a member of the options union */
typedef enum
{
  MLF_NONE,
  MLF_REGEXP,
  MLF_TIMESTAMP,
} MultiLineFamily;

static MultiLineFamily
_family_of(gint mode)
{
  switch (mode)
    {
    case MLM_REGEXP_PREFIX_GARBAGE:
    case MLM_REGEXP_PREFIX_SUFFIX:
      return MLF_REGEXP;
    case MLM_TIMESTAMP:
      return MLF_TIMESTAMP;
    default:
      return MLF_NONE;
    }
}

static gboolean
_union_in_use(const MultiLineOptions *options)
{
  switch (_family_of(options->mode))
    {
    case MLF_REGEXP:
      return options->regexp.prefix || options->regexp.garbage;
    case MLF_TIMESTAMP:
      return options->timestamp.pairs != NULL;
    default:
      return FALSE;
    }
}

static void
_release_union(MultiLineOptions *options)
{
  switch (_family_of(options->mode))
    {
    case MLF_REGEXP:
      multi_line_pattern_unref(options->regexp.prefix);
      multi_line_pattern_unref(options->regexp.garbage);
      break;
    case MLF_TIMESTAMP:
      g_free(options->timestamp.pairs);
      break;
    default:
      break;
    }
  /* regexp is the largest member */
  memset(&options->regexp, 0, sizeof(options->regexp));
}

/* an option of @family binds the mode to it while the mode is still unset */
static gboolean
_bind_family(MultiLineOptions *options, MultiLineFamily family, gint default_mode)
{
  if (options->mode == MLM_NONE)
    options->mode = default_mode;
  return _family_of(options->mode) == family;
}

static gboolean
_parse_mode(const gchar *mode, gint *result)
{
  if (strcasecmp(mode, "indented") == 0)
    *result = MLM_INDENTED;
  else if (strcasecmp(mode, "regexp") == 0)
    *result = MLM_REGEXP_PREFIX_GARBAGE;
  else if (strcasecmp(mode, "prefix-garbage") == 0)
    *result = MLM_REGEXP_PREFIX_GARBAGE;
  else if (strcasecmp(mode, "prefix-suffix") == 0)
    *result = MLM_REGEXP_PREFIX_SUFFIX;
  else if (strcasecmp(mode, "smart") == 0)
    *result = MLM_SMART;
  else if (strcasecmp(mode, "timestamp") == 0)
    *result = MLM_TIMESTAMP;
  else if (strcasecmp(mode, "none") == 0)
    *result = MLM_NONE;
  else
    return FALSE;
  return TRUE;
}

gboolean
multi_line_options_set_mode(MultiLineOptions *options, const gchar *mode)
{
  gint new_mode;

  if (!_parse_mode(mode, &new_mode))
    return FALSE;

  if (_union_in_use(options) && _family_of(new_mode) != _family_of(options->mode))
    {
      msg_error("multi-line-mode() conflicts with the multi-line options set before it",
                evt_tag_str("mode", mode));
      return FALSE;
    }
  options->mode = new_mode;
  return TRUE;
}

static GQuark
_error_quark(void)
{
  return g_quark_from_static_string("multi-line-options");
}

static gboolean
_bind_regexp_family(MultiLineOptions *options, const gchar *option, GError **error)
{
  if (_bind_family(options, MLF_REGEXP, MLM_REGEXP_PREFIX_GARBAGE))
    return TRUE;

  g_set_error(error, _error_quark(), 0,
              "%s needs a regexp based multi-line-mode() (prefix-garbage or prefix-suffix)", option);
  return FALSE;
}

gboolean
multi_line_options_set_prefix(MultiLineOptions *options, const gchar *prefix_regexp,
                              GError **error)
{
  if (!_bind_regexp_family(options, "multi-line-prefix()", error))
    return FALSE;

  multi_line_pattern_unref(options->regexp.prefix);
  options->regexp.prefix = multi_line_pattern_compile(prefix_regexp, error);
  return options->regexp.prefix != NULL;
}

gboolean
multi_line_options_set_garbage(MultiLineOptions *options, const gchar *garbage_regexp,
                               GError **error)
{
  if (!_bind_regexp_family(options, "multi-line-garbage()", error))
    return FALSE;

  multi_line_pattern_unref(options->regexp.garbage);
  options->regexp.garbage = multi_line_pattern_compile(garbage_regexp, error);
  return options->regexp.garbage != NULL;
}

gboolean
multi_line_options_set_timestamp_pairs(MultiLineOptions *options, const gchar *pairs)
{
  if (!_bind_family(options, MLF_TIMESTAMP, MLM_TIMESTAMP))
    {
      msg_error("multi-line-timestamp-pairs() needs multi-line-mode(timestamp)");
      return FALSE;
    }
  if (strlen(pairs) % 2 != 0)
    {
      msg_error("multi-line-timestamp-pairs() takes open/close character pairs, an even number of characters",
                evt_tag_str("pairs", pairs));
      return FALSE;
    }

  g_free(options->timestamp.pairs);
  options->timestamp.pairs = g_strdup(pairs);
  return TRUE;
}

gboolean
multi_line_options_validate(MultiLineOptions *options)
{
  /* the setters keep the mode and the union consistent */
  return TRUE;
}

void
multi_line_options_defaults(MultiLineOptions *options)
{
  memset(options, 0, sizeof(*options));
  options->mode = MLM_NONE;
}

void
multi_line_options_copy(MultiLineOptions *dest, MultiLineOptions *source)
{
  dest->mode = source->mode;
  switch (_family_of(dest->mode))
    {
    case MLF_REGEXP:
      dest->regexp.prefix = multi_line_pattern_ref(source->regexp.prefix);
      dest->regexp.garbage = multi_line_pattern_ref(source->regexp.garbage);
      break;
    case MLF_TIMESTAMP:
      dest->timestamp.pairs = g_strdup(source->timestamp.pairs);
      break;
    default:
      break;
    }
}

gboolean
multi_line_options_init(MultiLineOptions *options)
{
  if (!multi_line_options_validate(options))
    return FALSE;
  return TRUE;
}

void
multi_line_options_destroy(MultiLineOptions *options)
{
  _release_union(options);
}

void
multi_line_global_init(void)
{
  smart_multi_line_global_init();
  timestamp_multi_line_global_init();
}

void
multi_line_global_deinit(void)
{
  smart_multi_line_global_deinit();
  timestamp_multi_line_global_deinit();
}

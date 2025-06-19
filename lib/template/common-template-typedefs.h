/*
 * Copyright (c) 2014 Balabit
 * Copyright (c) 2014 Laszlo Budai
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

#ifndef COMMON_TYPEDEFS_H_INCLUDED
#define COMMON_TYPEDEFS_H_INCLUDED

#include "timeutils/zoneinfo.h"

#define LTZ_LOCAL 0
#define LTZ_SEND  1
#define LTZ_MAX   2

typedef struct _LogTemplateOptions LogTemplateOptions;
typedef struct _LogTemplate LogTemplate;

/* template expansion options that can be influenced by the user and
 * is static throughout the runtime for a given configuration. There
 * are call-site specific options too, those are specified as
 * arguments to log_template_format() */
struct _LogTemplateOptions
{
  gboolean initialized;
  /* timestamp format as specified by ts_format() */
  gint ts_format;
  /* number of digits in the fraction of a second part, specified using frac_digits() */
  gint frac_digits;
  gboolean use_fqdn;
  gboolean escape;

  /* timezone for LTZ_LOCAL/LTZ_SEND settings */
  gchar *time_zone[LTZ_MAX];
  TimeZoneInfo *time_zone_info[LTZ_MAX];

  /* Template error handling settings */
  gint on_error;
};

#endif

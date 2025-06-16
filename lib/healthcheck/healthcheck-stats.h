/*
 * Copyright (c) 2023 László Várady <laszlo.varady93@gmail.com>
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

#ifndef HEALTHCHECK_STATS_H
#define HEALTHCHECK_STATS_H

#include "syslog-ng.h"

typedef struct _HealthCheckStatsOptions
{
  gint freq;
} HealthCheckStatsOptions;

static inline void
healthcheck_stats_options_defaults(HealthCheckStatsOptions *options)
{
  options->freq = 300;
}

void healthcheck_stats_init(HealthCheckStatsOptions *options);
void healthcheck_stats_deinit(void);

void healthcheck_stats_global_init(void);

#endif

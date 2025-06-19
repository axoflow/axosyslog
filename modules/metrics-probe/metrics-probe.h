/*
 * Copyright (c) 2023 Attila Szakacs
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

#ifndef METRICS_PROBE_H_INCLUDED
#define METRICS_PROBE_H_INCLUDED

#include "metrics/dyn-metrics-template.h"
#include "parser/parser-expr.h"
#include "template/templates.h"

LogParser *metrics_probe_new(GlobalConfig *cfg);
void metrics_probe_set_increment_template(LogParser *s, LogTemplate *increment_template);

LogTemplateOptions *metrics_probe_get_template_options(LogParser *s);
DynMetricsTemplate *metrics_probe_get_metrics_template(LogParser *s);

#endif

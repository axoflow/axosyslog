/*
 * Copyright (c) 2026 Axoflow
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

#ifndef HTTPSOURCE_H
#define HTTPSOURCE_H

#include "driver.h"
#include "logsource.h"
#include "transport/tls-context.h"

typedef struct HTTPSourceDriver HTTPSourceDriver;

LogDriver *http_sd_new(GlobalConfig *cfg);

void http_sd_set_port(LogDriver *s, gint port);
void http_sd_set_bind_addr(LogDriver *s, const gchar *addr);
void http_sd_set_path(LogDriver *s, const gchar *path);
void http_sd_set_max_request_size(LogDriver *s, gsize size);
void http_sd_set_timeout(LogDriver *s, gint timeout);
void http_sd_set_tls_context(LogDriver *s, TLSContext *tls_context);

/* exposed for the grammar's source_option rules (sets last_source_options) */
LogSourceOptions *http_sd_get_source_options(LogDriver *s);

#endif

/*
 * Copyright (c) 2023 Ricardo Filipe <ricardo.l.filipe@tecnico.ulisboa.pt>
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

#ifndef TLS_TEST_VALIDATION_H_INCLUDED
#define TLS_TEST_VALIDATION_H_INCLUDED

#include "driver.h"

typedef struct _TlsTestValidationPlugin TlsTestValidationPlugin;

TlsTestValidationPlugin *tls_test_validation_plugin_new(void);

void tls_test_validation_plugin_set_identity(TlsTestValidationPlugin *self, const gchar *identity);

#endif

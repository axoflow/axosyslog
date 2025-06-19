/*
 * Copyright (c) 2016 Balabit
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

#ifndef CONTEXTUAL_DATA_RECORD_SCANNER_H_INCLUDED
#define CONTEXTUAL_DATA_RECORD_SCANNER_H_INCLUDED

#include "contextual-data-record.h"


typedef struct _ContextualDataRecordScanner ContextualDataRecordScanner;

ContextualDataRecord *contextual_data_record_scanner_get_next(ContextualDataRecordScanner *self,
    const gchar *input,
    const gchar *filename,
    gint lineno);

ContextualDataRecordScanner *contextual_data_record_scanner_new(GlobalConfig *cfg, const gchar *name_prefix);
void contextual_data_record_scanner_free(ContextualDataRecordScanner *self);

#endif

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

#ifndef CONTEXTINFODB_H_INCLUDED
#define CONTEXTINFODB_H_INCLUDED

#include "syslog-ng.h"
#include "contextual-data-record-scanner.h"
#include <stdio.h>

typedef struct _ContextInfoDB ContextInfoDB;

typedef void (*ADD_CONTEXT_INFO_CB) (gpointer arg,
                                     const ContextualDataRecord *record);

void context_info_db_enable_ordering(ContextInfoDB *self);
GList *context_info_db_ordered_selectors(ContextInfoDB *self);
void context_info_db_init(ContextInfoDB *self);

void context_info_db_purge(ContextInfoDB *self);
void context_info_db_index(ContextInfoDB *self);
gboolean context_info_db_is_loaded(const ContextInfoDB *self);
gboolean context_info_db_is_indexed(const ContextInfoDB *self);

void context_info_db_insert(ContextInfoDB *self,
                            const ContextualDataRecord *record);

gboolean context_info_db_contains(ContextInfoDB *self,
                                  const gchar *selector);

gsize context_info_db_number_of_records(ContextInfoDB *self,
                                        const gchar *selector);

void context_info_db_foreach_record(ContextInfoDB *self,
                                    const gchar *selector,
                                    ADD_CONTEXT_INFO_CB callback,
                                    gpointer arg);

GList *context_info_db_get_selectors(ContextInfoDB *self);

gboolean context_info_db_import(ContextInfoDB *self, FILE *fp, const gchar *filename,
                                ContextualDataRecordScanner *scanner);


ContextInfoDB *context_info_db_new(gboolean ignore_case);
ContextInfoDB *context_info_db_ref(ContextInfoDB *self);
void context_info_db_unref(ContextInfoDB *self);

#endif

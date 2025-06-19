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

#include "contextual-data-record.h"

void
contextual_data_record_init(ContextualDataRecord *record)
{
  record->selector = NULL;
  record->value_handle = 0;
  record->value = NULL;
}

void
contextual_data_record_clean(ContextualDataRecord *record)
{
  g_free(record->selector);
  log_template_unref(record->value);
  contextual_data_record_init(record);
}

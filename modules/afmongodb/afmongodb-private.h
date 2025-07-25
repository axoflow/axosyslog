/*
 * Copyright (c) 2010-2016 Balabit
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

#ifndef AFMONGODB_PRIVATE_H_
#define AFMONGODB_PRIVATE_H_

#include "syslog-ng.h"
#include "mongoc.h"
#include "logthrdest/logthrdestdrv.h"
#include "template/templates.h"
#include "value-pairs/value-pairs.h"

typedef struct _MongoDBDestDriver
{
  LogThreadedDestDriver super;

  GString *uri_str;
  LogTemplate *collection_template;
  gboolean collection_is_literal_string;

  LogTemplateOptions template_options;

  gboolean use_bulk;
  gboolean bulk_unordered;
  gboolean bulk_bypass_validation;
  int32_t write_concern_level;

  ValuePairs *vp;

  const gchar *const_db;
  mongoc_uri_t *uri_obj;
  mongoc_client_pool_t *pool;
} MongoDBDestDriver;

#endif

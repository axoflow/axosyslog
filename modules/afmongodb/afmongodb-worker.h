/*
 * Copyright (c) 2010-2021 One Identity
 * Copyright (c) 2010-2014 Gergely Nagy <algernon@balabit.hu>
 * Copyright (c) 2021 László Várady
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

#ifndef AFMONGODB_WORKER_H_INCLUDED
#define AFMONGODB_WORKER_H_INCLUDED

#include "syslog-ng.h"
#include "mongoc.h"
#include "logthrdest/logthrdestdrv.h"

typedef struct MongoDBDestWorker
{
  LogThreadedDestWorker super;

  mongoc_client_t *client;

  GString *collection;
  mongoc_collection_t *coll_obj;
  mongoc_bulk_operation_t *bulk_op;
  mongoc_write_concern_t *write_concern;

  bson_t *bson;
  bson_t *bson_opts;
} MongoDBDestWorker;

LogThreadedDestWorker *afmongodb_dw_new(LogThreadedDestDriver *owner, gint worker_index);

#endif

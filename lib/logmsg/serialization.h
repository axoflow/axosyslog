/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Balazs Scheidler <balazs.scheidler@balabit.com>
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


#ifndef LOGMSG_SERIALIZATION_H
#define LOGMSG_SERIALIZATION_H

#include "serialize.h"
#include "logmsg/logmsg.h"
#include "timeutils/unixtime.h"

typedef struct _LogMessageSerializationState
{
  guint8 version;
  SerializeArchive *sa;
  LogMessage *msg;
  NVTable *nvtable;
  guint8 nvtable_flags;
  guint8 handle_changed;
  NVHandle *updated_sdata_handles;
  NVIndexEntry *updated_index;
  const UnixTime *processed;
  guint32 flags;
} LogMessageSerializationState;

#endif

/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
 * Copyright (c) 2012-2013 Viktor Juhasz
 * Copyright (c) 2012-2013 Viktor Tusa
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

#ifndef NVTABLE_SERIALIZE_LEGACY_H_
#define NVTABLE_SERIALIZE_LEGACY_H_

#include "nvtable.h"
#include "serialize.h"

/*
 * Contains the deserialization functions of old NVTable versions.
 * We should be able to deserialize all previous versions of NVTable or logmsg.
 */

NVTable *nv_table_deserialize_22(SerializeArchive *sa);
NVTable *nv_table_deserialize_legacy(SerializeArchive *sa);

#endif /* NVTABLE_SERIALIZE_LEGACY_H_ */

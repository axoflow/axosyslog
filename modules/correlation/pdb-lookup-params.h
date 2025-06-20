/*
 * Copyright (c) 2002-2017 Balabit
 * Copyright (c) 1998-2017 Balázs Scheidler
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

#ifndef CORRELATION_PDB_LOOKUP_PARAMS_H_INCLUDED
#define CORRELATION_PDB_LOOKUP_PARAMS_H_INCLUDED

#include <template/templates.h>
#include "logmsg/logmsg.h"

typedef struct _PDBLookupParams PDBLookupParams;
struct _PDBLookupParams
{
  LogMessage *msg;
  NVHandle program_handle;
  LogTemplate *program_template;
  NVHandle message_handle;
  const gchar *message_string;
  gssize message_len;
};

void pdb_lookup_params_init(PDBLookupParams *lookup, LogMessage *msg, LogTemplate *program_template);

static inline void
pdb_lookup_params_override_message(PDBLookupParams *lookup, const gchar *message, gssize message_len)
{
  lookup->message_handle = LM_V_NONE;
  lookup->message_string = message;
  lookup->message_len = message_len;
}
#endif

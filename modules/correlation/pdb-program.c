/*
 * Copyright (c) 2002-2013, 2015 Balabit
 * Copyright (c) 1998-2013, 2015 Balázs Scheidler
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
#include "pdb-program.h"
#include "pdb-rule.h"

/*
 * Database based parser. The patterns are stored in an XML database.
 * Data structure is:
 *   - Parser -> programs -> rules -> patterns
 */

PDBProgram *
pdb_program_new(void)
{
  PDBProgram *self = g_new0(PDBProgram, 1);

  self->rules = r_new_node("", NULL);
  self->ref_cnt = 1;
  return self;
}

PDBProgram *
pdb_program_ref(PDBProgram *self)
{
  self->ref_cnt++;
  return self;
}

void
pdb_program_unref(PDBProgram *s)
{
  PDBProgram *self = (PDBProgram *) s;

  if (--self->ref_cnt == 0)
    {
      if (self->rules)
        r_free_node(self->rules, (void (*)(void *)) pdb_rule_unref);

      g_free(self->pdb_location);
      g_free(self);
    }
}

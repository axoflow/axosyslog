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

#ifndef CFG_BLOCK_GENERATOR_H_INCLUDED
#define CFG_BLOCK_GENERATOR_H_INCLUDED 1

#include "syslog-ng.h"

/**
 * CfgBlockGenerator:
 *
 * This class describes a block generator, e.g. a function callback
 * that returns a configuration snippet in a given context. Each
 * user-defined "block" results in a generator to be registered, but
 * theoretically this mechanism can be used to write plugins that
 * generate syslog-ng configuration on the fly, based on system
 * settings for example.
 **/
typedef struct _CfgBlockGenerator CfgBlockGenerator;
struct _CfgBlockGenerator
{
  gint ref_cnt;
  gint context;
  gchar *name;
  gboolean suppress_backticks;
  const gchar *(*format_name)(CfgBlockGenerator *self, gchar *buf, gsize buf_len);
  gboolean (*generate)(CfgBlockGenerator *self, GlobalConfig *cfg, gpointer args, GString *result,
                       const gchar *reference);
  void (*free_fn)(CfgBlockGenerator *self);
};

static inline const gchar *
cfg_block_generator_format_name(CfgBlockGenerator *self, gchar *buf, gsize buf_len)
{
  return self->format_name(self, buf, buf_len);
}

gboolean cfg_block_generator_generate(CfgBlockGenerator *self, GlobalConfig *cfg, gpointer args, GString *result,
                                      const gchar *reference);
void cfg_block_generator_init_instance(CfgBlockGenerator *self, gint context, const gchar *name);
void cfg_block_generator_free_method(CfgBlockGenerator *self);
CfgBlockGenerator *cfg_block_generator_ref(CfgBlockGenerator *self);
void cfg_block_generator_unref(CfgBlockGenerator *self);


#endif

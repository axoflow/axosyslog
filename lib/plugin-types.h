/*
 * Copyright (c) 2013 Balabit
 * Copyright (c) 2013 Balázs Scheidler <bazsi@balabit.hu>
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

#ifndef PLUGIN_TYPES_H_INCLUDED
#define PLUGIN_TYPES_H_INCLUDED

/*
 * Plugin types are defined by the configuration grammar (LL_CONTEXT_*)
 * tokens, but in order to avoid having to directly include cfg-grammar.h
 * (and for the ensuing WTF comments) define this headers to make it a bit
 * more visible that the reason for including that is the definition of
 * those tokens.
 *
 * In the future we might as well generate this header file, but for now we
 * simply poison the namespace with all bison generated macros, as no
 * practical issue has come up until now.
 */

#include "cfg-grammar.h"


/* bitmask to cover non-context flags in the "plugin_type", e.g.
 * LL_CONTEXT_FLAG_* */

#define LL_CONTEXT_FLAGS 0xF00

/* indicates that a plugin is actually a generator, e.g.  it is registered
 * as LL_CONTEXT_SOURCE but instead of running through the normal plugin
 * instantiation via Plugin->parse, let it generate a configuration snippet
 * that will be parsed normally.
 */

#define LL_CONTEXT_FLAG_GENERATOR 0x0100

#endif

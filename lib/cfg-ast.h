/*
 * Copyright (c) 2026 Axoflow
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

#ifndef CFG_AST_H_INCLUDED
#define CFG_AST_H_INCLUDED

#include "cfg-tree.h"

/* Format the abstract syntax tree of an already parsed configuration as JSON.
 * The returned GString is owned by the caller.
 *
 * The statements of the dump are the root level log expressions of the CfgTree
 * (sorted by their source location, as the tree itself only keeps the order of
 * the log statements), each with its expression tree and the parsed filterx
 * expressions inside.  The statements that are not part of the CfgTree are not
 * reported: options{}, template{}, template-function, block{} definitions and
 * root level plugins such as python{}.
 *
 * The configuration must not be initialized yet, as cfg_init() rewrites the
 * nodes of the tree: log paths with flags(catch-all) get their sources
 * substituted and the anonymous expressions are named.
 */
GString *cfg_ast_format(GlobalConfig *cfg);

#endif

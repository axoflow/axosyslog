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
#ifndef FILTERX_TYPE_INFERENCE_PRIVATE_H_INCLUDED
#define FILTERX_TYPE_INFERENCE_PRIVATE_H_INCLUDED

#include "filterx/filterx-type-inference.h"
#include "filterx/filterx-expr.h"

/* Not part of the public interface: every compiler-facing caller goes through
 * filterx_type_env_get_for_expr() instead.  Exposed here so the path/env test suites can exercise
 * path-keyed reads directly. */

/* The kind at @path: its own entry, or UNKNOWN when there is none.  Nothing is inherited from an
 * ancestor -- an entry is the only thing that speaks for a location. */
FilterXStaticType filterx_type_env_get_at_path(const FilterXTypeEnv *self, const FilterXTypePath *path);

#endif

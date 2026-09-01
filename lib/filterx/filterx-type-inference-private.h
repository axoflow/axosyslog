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

/* Path-keyed access, for the test suites.  Every compiler-facing caller goes through
 * filterx-type-inference.h instead. */

FilterXStaticType filterx_type_env_get_static_type_at_path(const FilterXTypeEnv *self, const FilterXAccessPath *path);

/* TRUE when @path has an entry of its own, which is a claim that the value exists.  Recorded at
 * UNKNOWN and unrecorded read the same through get_static_type_at_path(), and the meet turns on
 * exactly that difference. */
gboolean filterx_type_env_get_fact_at_path(const FilterXTypeEnv *self, const FilterXAccessPath *path,
                                           FilterXStaticType *static_type_out, gboolean *closed_out);

void filterx_type_env_set_at_path(FilterXTypeEnv *self, const FilterXAccessPath *path,
                                  FilterXStaticType static_type, gboolean closed);

/* Something mutated the container at @path in a way this pass cannot follow: keep its static type,
 * drop everything below it and stop claiming its key set is complete. */
void filterx_type_env_open_at_path(FilterXTypeEnv *self, const FilterXAccessPath *path);

/* unset() and move() prove @path gone, which is why the parent keeps `closed` here. */
void filterx_type_env_clear_at_path(FilterXTypeEnv *self, const FilterXAccessPath *path);

typedef gboolean (*FilterXTypeEnvHandlePredicate)(FilterXVariableHandle handle, gpointer user_data);

FilterXTypeEnv *filterx_type_env_clone_filtered(const FilterXTypeEnv *self,
                                                FilterXTypeEnvHandlePredicate pred, gpointer user_data);

#endif

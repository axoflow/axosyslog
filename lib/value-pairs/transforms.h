/*
 * Copyright (c) 2011-2013 Balabit
 * Copyright (c) 2011-2013 Gergely Nagy <algernon@balabit.hu>
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

#ifndef VPTRANSFORM_INCLUDED
#define VPTRANSFORM_INCLUDED 1

#include "syslog-ng.h"

typedef struct _ValuePairsTransform ValuePairsTransform;
typedef struct _ValuePairsTransformSet ValuePairsTransformSet;

ValuePairsTransform *value_pairs_new_transform_add_prefix (const gchar *prefix);
ValuePairsTransform *value_pairs_new_transform_lower (void);
ValuePairsTransform *value_pairs_new_transform_upper (void);
ValuePairsTransform *value_pairs_new_transform_shift (gint amount);
ValuePairsTransform *value_pairs_new_transform_replace_prefix(const gchar *prefix, const gchar *new_prefix);
ValuePairsTransform *value_pairs_new_transform_shift_levels(gint amount);
void value_pairs_transform_free(ValuePairsTransform *t);

ValuePairsTransformSet *value_pairs_transform_set_new(const gchar *glob);
void value_pairs_transform_set_add_func(ValuePairsTransformSet *vpts, ValuePairsTransform *vpt);
void value_pairs_transform_set_free(ValuePairsTransformSet *vpts);
void value_pairs_transform_set_apply(ValuePairsTransformSet *vpts, GString *key);

#endif

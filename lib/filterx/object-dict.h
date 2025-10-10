/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 László Várady
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

#ifndef FILTERX_OBJECT_DICT_H
#define FILTERX_OBJECT_DICT_H

#include "filterx/filterx-object.h"

FILTERX_DECLARE_TYPE(dict);

FilterXObject *filterx_dict_new(void);
FilterXObject *filterx_dict_sized_new(gsize init_size);
FilterXObject *filterx_dict_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len);

#endif

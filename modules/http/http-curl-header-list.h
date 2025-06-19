/*
 * Copyright (c) 2002-2019 Balabit
 * Copyright (c) 2019 Laszlo Budai <laszlo.budai@balabit.com>
 *
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

#ifndef HTTP_CURL_HEADER_LIST_H_INCLUDED
#define HTTP_CURL_HEADER_LIST_H_INCLUDED

#include "list-adt.h"

#define CURL_NO_OLDIES 1
#include <curl/curl.h>

List *http_curl_header_list_new(void);
struct curl_slist *http_curl_header_list_as_slist(List *list);

#endif

/*
 * Copyright (c) 2015 BalaBit
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
#ifndef CORRELATION_GROUPING_BY_PARSER_H_INCLUDED
#define CORRELATION_GROUPING_BY_PARSER_H_INCLUDED

#include "grouping-parser.h"
#include "synthetic-message.h"
#include "filter/filter-expr.h"

void grouping_by_set_key_template(LogParser *s, LogTemplate *context_id);
void grouping_by_set_sort_key_template(LogParser *s, LogTemplate *sort_key);
void grouping_by_set_timeout(LogParser *s, gint timeout);
void grouping_by_set_scope(LogParser *s, CorrelationScope scope);
void grouping_by_set_synthetic_message(LogParser *s, SyntheticMessage *message);
void grouping_by_set_trigger_condition(LogParser *s, FilterExprNode *filter_expr);
void grouping_by_set_where_condition(LogParser *s, FilterExprNode *filter_expr);
void grouping_by_set_having_condition(LogParser *s, FilterExprNode *filter_expr);
void grouping_by_set_prefix(LogParser *s, const gchar *prefix);
LogParser *grouping_by_new(GlobalConfig *cfg);

#endif

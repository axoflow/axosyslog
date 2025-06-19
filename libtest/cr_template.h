/*
 * Copyright (c) 2012-2018 Balabit
 * Copyright (c) 2012 Balázs Scheidler
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

#ifndef LIBTEST_CR_TEMPLATE_H_INCLUDED
#define LIBTEST_CR_TEMPLATE_H_INCLUDED 1

#include "syslog-ng.h"
#include "template/templates.h"
#include "msg-format.h"
#include <stdarg.h>

void assert_template_format(const gchar *template, const gchar *expected);
void assert_template_format_msg(const gchar *template,
                                const gchar *expected, LogMessage *msg);
void assert_template_format_with_escaping(const gchar *template, gboolean escaping, const gchar *expected);
void assert_template_format_with_escaping_msg(const gchar *template, gboolean escaping,
                                              const gchar *expected, LogMessage *msg);
void assert_template_format_with_context(const gchar *template, const gchar *expected);
void assert_template_format_with_context_msgs(const gchar *template, const gchar *expected, LogMessage **msgs,
                                              gint num_messages);
void assert_template_format_with_len(const gchar *template, const gchar *expected, gssize expected_len);
void assert_template_format_with_escaping_and_context_msgs(const gchar *template, gboolean escaping,
                                                           const gchar *expected, gssize expected_len,
                                                           LogMessageValueType expected_type,
                                                           LogMessage **msgs, gint num_messages);
void assert_template_format_value_and_type(const gchar *template, const gchar *expected,
                                           LogMessageValueType expected_type);
void assert_template_format_value_and_type_msg(const gchar *template, const gchar *expected,
                                               LogMessageValueType expected_type, LogMessage *msg);
void assert_template_format_value_and_type_with_escaping(const gchar *template, gboolean escaping,
                                                         const gchar *expected, LogMessageValueType expected_type);

void assert_template_failure(const gchar *template, const gchar *expected_failure);
void perftest_template(gchar *template);

LogMessage *create_empty_message(void);
LogMessage *create_sample_message(void);
LogMessage *message_from_list(va_list ap);
LogTemplate *compile_template(const gchar *template);
LogTemplate *compile_escaped_template(const gchar *template);
LogTemplate *compile_template_with_escaping(const gchar *template, gboolean escaping);
void init_template_tests(void);
void deinit_template_tests(void);


#endif

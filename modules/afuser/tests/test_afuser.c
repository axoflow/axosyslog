/*
 * Copyright (c) 2026 One Identity
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 */

#include <criterion/criterion.h>
#include <string.h>

#include "afuser.h"
#include "apphook.h"
#include "cfg.h"
#include "libtest/config_parse_lib.h"
#include "libtest/cr_template.h"
#include "logmsg/logmsg.h"
#include "logpipe.h"

static GlobalConfig *cfg;

static LogMessage *
_create_test_message(const gchar *message)
{
  LogMessage *msg = create_empty_message();

  msg->timestamps[LM_TS_STAMP].ut_sec = 0;
  log_msg_set_value(msg, LM_V_HOST, "testhost", -1);
  log_msg_set_value(msg, LM_V_MESSAGE, message, -1);
  return msg;
}

static void
_setup(void)
{
  app_startup();
  configuration = cfg = cfg_new_snippet();
}

static void
_teardown(void)
{
  cfg_free(cfg);
  configuration = NULL;
  app_shutdown();
}

TestSuite(afuser, .init = _setup, .fini = _teardown);

Test(afuser, formats_raw_message_by_default)
{
  LogDriver *driver = afuser_dd_new("root", cfg);
  LogMessage *msg = _create_test_message("\033[31mfoo\033[0m");
  GString *formatted_message = g_string_sized_new(0);

  afuser_dd_format_message(driver, msg, formatted_message);

  cr_assert(strstr(formatted_message->str, "\033[31mfoo\033[0m\n") != NULL,
            "formatted_message='%s'", formatted_message->str);

  g_string_free(formatted_message, TRUE);
  log_msg_unref(msg);
  log_pipe_unref((LogPipe *) driver);
}

Test(afuser, formats_escaped_message_when_enabled)
{
  LogDriver *driver = afuser_dd_new("root", cfg);
  LogMessage *msg = _create_test_message("\033[31mfoo\033[0m");
  GString *formatted_message = g_string_sized_new(0);

  afuser_dd_set_escaping(driver, TRUE);
  afuser_dd_format_message(driver, msg, formatted_message);

  cr_assert(strstr(formatted_message->str, "\\033[31mfoo\\033[0m\n") != NULL,
            "formatted_message='%s'", formatted_message->str);

  g_string_free(formatted_message, TRUE);
  log_msg_unref(msg);
  log_pipe_unref((LogPipe *) driver);
}

Test(afuser, parses_escaping_option)
{
  cfg_load_module(cfg, "afuser");

  cr_assert(parse_config("destination d_test { usertty(\"root\" escaping(yes)); };",
                         LL_CONTEXT_ROOT, NULL, NULL),
            "Parsing the given configuration failed");
}

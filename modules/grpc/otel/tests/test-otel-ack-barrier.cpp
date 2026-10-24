/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

#include "otel-ack-barrier.hpp"

#include "compat/cpp-start.h"
#include "ack-tracker/ack_tracker.h"
#include "ack-tracker/ack_tracker_factory.h"
#include "logsource.h"
#include "apphook.h"
#include "cfg.h"
#include "compat/cpp-end.h"

#include <criterion/criterion.h>

using namespace syslogng::grpc::otel;

Test(otel_ack_barrier, seal_without_messages_opens_at_once)
{
  int opened_ctr = 0;
  AckBarrier *barrier = new AckBarrier([&] { opened_ctr++; });

  cr_assert(barrier->seal());
  cr_assert_eq(opened_ctr, 0);

  barrier->unref();
}

Test(otel_ack_barrier, opens_on_the_last_ack_after_seal)
{
  int opened_ctr = 0;
  AckBarrier *barrier = new AckBarrier([&] { opened_ctr++; });
  Bookmark bookmarks[2] = {};

  barrier->add_bookmark(&bookmarks[0]);
  barrier->add_bookmark(&bookmarks[1]);
  cr_assert_not(barrier->seal());

  bookmark_save(&bookmarks[0]);
  cr_assert_eq(opened_ctr, 0);

  bookmark_save(&bookmarks[1]);
  cr_assert_eq(opened_ctr, 1);

  bookmark_destroy(&bookmarks[0]);
  bookmark_destroy(&bookmarks[1]);
  barrier->unref();
}

Test(otel_ack_barrier, acks_before_seal_do_not_open)
{
  int opened_ctr = 0;
  AckBarrier *barrier = new AckBarrier([&] { opened_ctr++; });
  Bookmark bookmarks[2] = {};

  barrier->add_bookmark(&bookmarks[0]);
  barrier->add_bookmark(&bookmarks[1]);
  bookmark_save(&bookmarks[0]);
  bookmark_save(&bookmarks[1]);
  cr_assert_eq(opened_ctr, 0);

  cr_assert(barrier->seal());
  cr_assert_eq(opened_ctr, 0);

  bookmark_destroy(&bookmarks[0]);
  bookmark_destroy(&bookmarks[1]);
  barrier->unref();
}

Test(otel_ack_barrier, bookmarks_outlive_the_owner)
{
  int opened_ctr = 0;
  AckBarrier *barrier = new AckBarrier([&] { opened_ctr++; });
  Bookmark bookmark = {};

  barrier->add_bookmark(&bookmark);
  cr_assert_not(barrier->seal());
  barrier->unref();

  bookmark_save(&bookmark);
  cr_assert_eq(opened_ctr, 1);
  bookmark_destroy(&bookmark);
}

Test(otel_ack_barrier, detach_stops_the_callback)
{
  int opened_ctr = 0;
  AckBarrier *barrier = new AckBarrier([&] { opened_ctr++; });
  Bookmark bookmark = {};

  barrier->add_bookmark(&bookmark);
  cr_assert_not(barrier->seal());
  barrier->detach();
  barrier->unref();

  bookmark_save(&bookmark);
  cr_assert_eq(opened_ctr, 0);
  bookmark_destroy(&bookmark);
}

static GlobalConfig *cfg;

static void
_queue_without_ack(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options)
{
}

static LogPipe *
_new_ack_holding_pipe()
{
  LogPipe *pipe = g_new0(LogPipe, 1);

  log_pipe_init_instance(pipe, cfg);
  pipe->queue = _queue_without_ack;
  cr_assert(log_pipe_init(pipe));

  return pipe;
}

static LogSource *
_new_source(LogPipe *next)
{
  LogSource *src = g_new0(LogSource, 1);
  LogSourceOptions *options = g_new0(LogSourceOptions, 1);

  log_source_options_defaults(options);
  options->init_window_size = 10;
  log_source_init_instance(src, cfg);
  log_source_options_init(options, cfg, "testgroup");
  log_source_set_options(src, options, "test_stats_id", NULL, TRUE, NULL);
  log_source_set_ack_tracker_factory(src, instant_ack_tracker_factory_new());
  log_pipe_append(&src->super, next);
  cr_assert(log_pipe_init(&src->super));

  return src;
}

static void
_free_source(LogSource *src)
{
  log_pipe_deinit(&src->super);
  g_free(src->options);
  log_pipe_unref(&src->super);
}

static LogMessage *
_post(LogSource *src, AckBarrier *barrier)
{
  LogMessage *msg = log_msg_new_empty();

  barrier->add_bookmark(ack_tracker_request_bookmark(src->ack_tracker));
  log_source_post(src, msg);

  return msg;
}

static void
_ack(LogMessage *msg)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;

  log_msg_ack(msg, &path_options, AT_PROCESSED);
  log_msg_unref(msg);
}

static void
_setup()
{
  app_startup();
  cfg = cfg_new_snippet();
}

static void
_teardown()
{
  cfg_free(cfg);
  app_shutdown();
}

TestSuite(otel_ack_barrier_with_log_source, .init = _setup, .fini = _teardown);

Test(otel_ack_barrier_with_log_source, opens_when_every_posted_message_is_acked)
{
  LogPipe *next = _new_ack_holding_pipe();
  LogSource *src = _new_source(next);
  int opened_ctr = 0;
  AckBarrier *barrier = new AckBarrier([&] { opened_ctr++; });

  LogMessage *msg1 = _post(src, barrier);
  LogMessage *msg2 = _post(src, barrier);
  cr_assert_not(barrier->seal());

  _ack(msg1);
  cr_assert_eq(opened_ctr, 0);
  _ack(msg2);
  cr_assert_eq(opened_ctr, 1);

  barrier->unref();
  _free_source(src);
  log_pipe_deinit(next);
  log_pipe_unref(next);
}

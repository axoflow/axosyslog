/*
 * Copyright (c) 2002-2016 Balabit
 * Copyright (c) 2016 Viktor Juhasz <viktor.juhasz@balabit.com>
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

#include <criterion/criterion.h>
#include "libtest/queue_utils_lib.h"
#include "test_diskq_tools.h"

#include "messages.h"
#include "logqueue-disk.h"
#include "logqueue-disk-reliable.h"
#include "logqueue-disk-non-reliable.h"
#include "apphook.h"
#include "plugin.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DISKQ_FILENAME_NOT_RELIABLE "test_become_full_not_reliable.qf"
#define DISKQ_FILENAME_RELIABLE "test_become_full_reliable.qf"
#define DISKQ_FILENAME_NR_FLOW_CONTROL "test_non_reliable_flow_control.nqf"


static void msg_post_function(LogMessage *msg)
{
  log_msg_unref(msg);
}

void
test_diskq_become_full(gboolean reliable, const gchar *filename)
{
  LogQueue *q;
  acked_messages = 0;
  fed_messages = 0;
  DiskQueueOptions options = {0};

  const gchar *persist_name = "test_diskq";

  StatsClusterKeyBuilder *driver_sck_builder = stats_cluster_key_builder_new();
  StatsClusterKeyBuilder *queue_sck_builder = stats_cluster_key_builder_new();
  options.reliable = reliable;
  if (reliable)
    {
      _construct_options(&options, 1000, 1000, reliable);
      q = log_queue_disk_reliable_new(&options, filename, persist_name, STATS_LEVEL0, driver_sck_builder,
                                      queue_sck_builder);
    }
  else
    {
      _construct_options(&options, 1000, 0, reliable);
      q = log_queue_disk_non_reliable_new(&options, filename, persist_name, STATS_LEVEL0, driver_sck_builder,
                                          queue_sck_builder);
    }
  stats_cluster_key_builder_free(driver_sck_builder);
  stats_cluster_key_builder_free(queue_sck_builder);

  unlink(filename);
  log_queue_disk_start(q);
  feed_messages_without_flow_control(q, 1000);

  cr_assert_eq(atomic_gssize_racy_get(&q->metrics.shared.dropped_messages->value), 1000,
               "Bad dropped message number (reliable: %s): actual: %"G_GSSIZE_FORMAT", expected: %d",
               reliable ? "TRUE" : "FALSE",
               atomic_gssize_racy_get(&q->metrics.shared.dropped_messages->value),
               1000);

  gboolean persistent;
  log_queue_disk_stop(q, &persistent);
  log_queue_unref(q);
  disk_queue_options_destroy(&options);
  unlink(filename);
}

Test(diskq_full, diskq_become_full_reliable)
{
  test_diskq_become_full(TRUE, DISKQ_FILENAME_RELIABLE);
}

Test(diskq_full, diskq_become_full_non_reliable)
{
  test_diskq_become_full(FALSE, DISKQ_FILENAME_NOT_RELIABLE);
}

Test(diskq_full, diskq_non_reliable_flow_control)
{
  acked_messages = 0;
  fed_messages = 0;

  StatsClusterKeyBuilder *driver_sck_builder = stats_cluster_key_builder_new();
  StatsClusterKeyBuilder *queue_sck_builder = stats_cluster_key_builder_new();
  DiskQueueOptions options = {0};
  _construct_options(&options, 1000, 0, FALSE);
  LogQueue *q = log_queue_disk_non_reliable_new(&options, DISKQ_FILENAME_NR_FLOW_CONTROL, "test_nr_fc", STATS_LEVEL0,
                                                driver_sck_builder, queue_sck_builder);
  stats_cluster_key_builder_free(driver_sck_builder);
  stats_cluster_key_builder_free(queue_sck_builder);

  unlink(DISKQ_FILENAME_NR_FLOW_CONTROL);
  log_queue_disk_start(q);

  const int expected_dropped = 1000;
  feed_messages_without_flow_control(q, expected_dropped);

  cr_assert_eq(atomic_gssize_racy_get(&q->metrics.shared.dropped_messages->value), expected_dropped,
               "Bad dropped message number: actual: %"G_GSSIZE_FORMAT", expected: %d",
               atomic_gssize_racy_get(&q->metrics.shared.dropped_messages->value),
               expected_dropped);

  feed_some_messages(q, 10);
  cr_assert_eq(atomic_gssize_racy_get(&q->metrics.shared.dropped_messages->value), expected_dropped,
               "Flow-controlled messages should never be dropped: actual: %"G_GSSIZE_FORMAT", expected: %d",
               atomic_gssize_racy_get(&q->metrics.shared.dropped_messages->value),
               expected_dropped);

  gboolean persistent;
  log_queue_disk_stop(q, &persistent);
  log_queue_unref(q);
  disk_queue_options_destroy(&options);
  unlink(DISKQ_FILENAME_NR_FLOW_CONTROL);
}

static void
setup(void)
{
  app_startup();
  setenv("TZ", "MET-1METDST", TRUE);
  tzset();

  configuration = cfg_new_snippet();
  cfg_load_module(configuration, "disk-buffer");
  msg_set_post_func(msg_post_function);
}

static void
teardown(void)
{
  cfg_free(configuration);
  app_shutdown();
}

TestSuite(diskq_full, .init = setup, .fini = teardown);

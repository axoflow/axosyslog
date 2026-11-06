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

#include <criterion/criterion.h>

#include "http/http-ack-state.h"

static guint wakeups;
static LogProtoServerWakeupCallback wakeup;

static void
_count_wakeup(gpointer user_data)
{
  (*(guint *) user_data)++;
}

static void
_setup(void)
{
  wakeups = 0;
  wakeup.func = _count_wakeup;
  wakeup.user_data = &wakeups;
}

TestSuite(http_ack_state, .init = _setup);

Test(http_ack_state, set_wanted_when_durable_already_arms_nothing)
{
  HTTPAckState *state = http_ack_state_new(&wakeup);
  Bookmark bookmarks[2] = { 0 };

  http_ack_state_fill_bookmark(state, &bookmarks[0], 1);
  bookmark_save(&bookmarks[0]);
  bookmark_destroy(&bookmarks[0]);
  cr_assert_eq(http_ack_state_get_durable(state), 1);

  http_ack_state_set_wanted(state, 1);
  http_ack_state_fill_bookmark(state, &bookmarks[1], 2);
  bookmark_save(&bookmarks[1]);
  bookmark_destroy(&bookmarks[1]);
  cr_assert_eq(wakeups, 0);

  http_ack_state_unref(state);
}

Test(http_ack_state, wakes_once_at_the_wanted_position)
{
  HTTPAckState *state = http_ack_state_new(&wakeup);
  Bookmark bookmarks[3] = { 0 };

  http_ack_state_fill_bookmark(state, &bookmarks[0], 1);
  http_ack_state_fill_bookmark(state, &bookmarks[1], 2);
  http_ack_state_fill_bookmark(state, &bookmarks[2], 3);
  http_ack_state_set_wanted(state, 2);

  bookmark_save(&bookmarks[0]);
  cr_assert_eq(wakeups, 0);
  bookmark_save(&bookmarks[1]);
  cr_assert_eq(wakeups, 1);
  cr_assert_eq(http_ack_state_get_durable(state), 2);
  bookmark_save(&bookmarks[2]);
  cr_assert_eq(wakeups, 1);

  for (gint i = 0; i < 3; i++)
    bookmark_destroy(&bookmarks[i]);
  cr_assert_not(http_ack_state_is_aborted(state));

  http_ack_state_unref(state);
}

Test(http_ack_state, a_bookmark_destroyed_without_a_save_is_an_abort)
{
  HTTPAckState *state = http_ack_state_new(&wakeup);
  Bookmark bookmark = { 0 };

  http_ack_state_fill_bookmark(state, &bookmark, 1);
  http_ack_state_set_wanted(state, 1);
  bookmark_destroy(&bookmark);

  cr_assert(http_ack_state_is_aborted(state));
  cr_assert_eq(wakeups, 1);
  cr_assert_eq(http_ack_state_get_durable(state), 0);

  http_ack_state_unref(state);
}

Test(http_ack_state, refilling_a_bookmark_releases_the_earlier_fill)
{
  HTTPAckState *state = http_ack_state_new(&wakeup);
  Bookmark bookmark = { 0 };

  http_ack_state_fill_bookmark(state, &bookmark, 1);
  http_ack_state_fill_bookmark(state, &bookmark, 1);
  http_ack_state_set_wanted(state, 1);
  bookmark_save(&bookmark);
  bookmark_destroy(&bookmark);

  cr_assert_eq(wakeups, 1);
  cr_assert_not(http_ack_state_is_aborted(state));

  http_ack_state_unref(state);
}

Test(http_ack_state, detach_stops_the_wakeups)
{
  HTTPAckState *state = http_ack_state_new(&wakeup);
  Bookmark bookmark = { 0 };

  http_ack_state_fill_bookmark(state, &bookmark, 1);
  http_ack_state_set_wanted(state, 1);
  http_ack_state_detach(state);
  http_ack_state_unref(state);

  bookmark_save(&bookmark);
  bookmark_destroy(&bookmark);
  cr_assert_eq(wakeups, 0);
}

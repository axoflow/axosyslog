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

#ifndef OTEL_ACK_BARRIER_HPP
#define OTEL_ACK_BARRIER_HPP

#include <atomic>
#include <functional>
#include <mutex>

#include "compat/cpp-start.h"
#include "ack-tracker/bookmark.h"
#include "compat/cpp-end.h"

namespace syslogng {
namespace grpc {
namespace otel {

/* Opens once the owner sealed it and every message added to it has been
 * acknowledged.  The messages are added through the Bookmark of a
 * bookmark-capable AckTracker, which calls back on the ACK of each.
 *
 * The opened callback runs in the thread of the last acknowledgement, so it
 * may only hand over to the owner's thread, never act on its state.  An
 * owner that goes away before the barrier opens detaches first: a callback
 * in progress completes before detach() returns, none starts after.
 */
class AckBarrier
{
public:
  explicit AckBarrier(std::function<void()> opened);

  AckBarrier *ref();
  void unref();

  void add_bookmark(Bookmark *bookmark);

  /* No further messages will be added.  Returns true when every message
   * added so far has already been acknowledged, in which case the opened
   * callback is not called.
   */
  bool seal();
  void detach();

private:
  ~AckBarrier() = default;
  static void _bookmark_save(Bookmark *bookmark);
  static void _bookmark_destroy(Bookmark *bookmark);

  std::atomic<int> ref_cnt{1};
  /* one for the owner until sealed, one per message until its ACK */
  std::atomic<int> pending{1};
  std::mutex lock;
  std::function<void()> opened;
};

}
}
}

#endif

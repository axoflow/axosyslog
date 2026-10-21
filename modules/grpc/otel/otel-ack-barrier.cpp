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

using namespace syslogng::grpc::otel;

namespace {

struct BookmarkData
{
  AckBarrier *barrier;
};

static_assert(sizeof(BookmarkData) <= sizeof(BookmarkContainer), "BookmarkData must fit the Bookmark");

BookmarkData *
_data_of(Bookmark *bookmark)
{
  return reinterpret_cast<BookmarkData *>(&bookmark->container);
}

}

AckBarrier::AckBarrier(std::function<void()> opened_)
  : opened(std::move(opened_))
{
}

AckBarrier *
AckBarrier::ref()
{
  ref_cnt.fetch_add(1);
  return this;
}

void
AckBarrier::unref()
{
  if (ref_cnt.fetch_sub(1) == 1)
    delete this;
}

void
AckBarrier::add_bookmark(Bookmark *bookmark)
{
  _data_of(bookmark)->barrier = ref();
  bookmark->save = _bookmark_save;
  bookmark->destroy = _bookmark_destroy;
  pending.fetch_add(1);
}

bool
AckBarrier::seal()
{
  return pending.fetch_sub(1) == 1;
}

void
AckBarrier::detach()
{
  std::lock_guard<std::mutex> guard(lock);
  opened = nullptr;
}

void
AckBarrier::_bookmark_save(Bookmark *bookmark)
{
  AckBarrier *self = _data_of(bookmark)->barrier;

  if (self->pending.fetch_sub(1) != 1)
    return;

  std::lock_guard<std::mutex> guard(self->lock);
  if (self->opened)
    self->opened();
}

void
AckBarrier::_bookmark_destroy(Bookmark *bookmark)
{
  _data_of(bookmark)->barrier->unref();
}

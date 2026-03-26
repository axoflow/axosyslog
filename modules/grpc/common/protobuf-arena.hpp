/*
 * Copyright (c) 2026 Axoflow
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

#ifndef PROTOBUF_ARENA_HPP
#define PROTOBUF_ARENA_HPP

#include <google/protobuf/arena.h>
#include <google/protobuf/stubs/common.h>

#include <memory>
#include <vector>

namespace syslogng {
namespace grpc {

class SmartArena
{
  static constexpr size_t DefaultInitialSize = 256;

private:
  std::vector<char> arena_buffer;
  std::unique_ptr<google::protobuf::Arena> arena;

public:
  SmartArena(size_t initial_size = DefaultInitialSize);

  google::protobuf::Arena *get();

  template <typename T>
  T *CreateMessage()
  {
#if GOOGLE_PROTOBUF_VERSION >= 5026000
    return google::protobuf::Arena::Create<T>(arena.get());
#else
    return google::protobuf::Arena::CreateMessage<T>(arena.get());
#endif
  }

  size_t SpaceAllocated();
  size_t SpaceUsed();
  uint64_t Reset();
};

}
}

#endif

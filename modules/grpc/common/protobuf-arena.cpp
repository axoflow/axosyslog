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

#include "protobuf-arena.hpp"

#include <algorithm>

using namespace syslogng::grpc;

static std::unique_ptr<google::protobuf::Arena>
_make_arena(char *buffer, size_t size)
{
  google::protobuf::ArenaOptions options;
  options.initial_block = buffer;
  options.initial_block_size = size;
  return std::unique_ptr<google::protobuf::Arena>(new google::protobuf::Arena(options));
}

SmartArena::SmartArena(size_t initial_size)
  : arena_buffer(initial_size),
    arena(_make_arena(arena_buffer.data(), arena_buffer.size()))
{}

size_t SmartArena::DefaultInitialSize = 256;

google::protobuf::Arena *
SmartArena::get()
{
  return arena.get();
}

size_t
SmartArena::SpaceAllocated()
{
  return arena->SpaceAllocated();
}

size_t
SmartArena::SpaceUsed()
{
  return arena->SpaceUsed();
}

uint64_t
SmartArena::Reset()
{
  size_t used = arena->SpaceUsed();
  size_t current_size = arena_buffer.size();

  if (used >= 2 * current_size || used * 2 <= current_size)
    {
      size_t new_size = std::max(used, DefaultInitialSize);
      arena.reset();
      arena_buffer.resize(new_size);
      arena = _make_arena(arena_buffer.data(), arena_buffer.size());
      return used;
    }

  return arena->Reset();
}

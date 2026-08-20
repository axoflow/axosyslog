/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 László Várady
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

#include <glib.h>
#include <string.h>

typedef struct _Buffer
{
  guint8 *buffer;
  gsize size;
  gsize capacity;
  gsize consumed;
} Buffer;

static inline gboolean
buffer_allocated(const Buffer *buffer)
{
  return buffer->buffer != NULL;
}

static inline void
buffer_reset(Buffer *buffer)
{
  buffer->size = 0;
  buffer->consumed = 0;
}

static inline void
buffer_allocate(Buffer *buffer, gsize capacity)
{
  buffer->capacity = capacity;
  buffer->buffer = g_malloc(capacity);
  buffer_reset(buffer);
}

static inline void
buffer_deallocate(Buffer *buffer)
{
  buffer_reset(buffer);
  buffer->capacity = 0;
  g_free(buffer->buffer);
  buffer->buffer = NULL;
}

static inline void
buffer_assign(Buffer *buffer, guint8 *data, gsize size)
{
  buffer_deallocate(buffer);
  buffer->buffer = data;
  buffer->capacity = buffer->size = size;
}

static inline guint8 *
buffer_start(const Buffer *buffer)
{
  return &buffer->buffer[buffer->consumed];
}

static inline guint8 *
buffer_end(const Buffer *buffer)
{
  return &buffer->buffer[buffer->size];
}

static inline gsize
buffer_unused_capacity(const Buffer *buffer)
{
  return buffer->capacity - buffer->size;
}

static inline void
buffer_increase_size(Buffer *buffer, gsize amount)
{
  buffer->size += amount;
}

static inline void
buffer_consume(Buffer *buffer, gsize amount)
{
  buffer->consumed += amount;
}

static inline gsize
buffer_size(const Buffer *buffer)
{
  return buffer->size - buffer->consumed;
}

static inline gboolean
buffer_is_empty(const Buffer *buffer)
{
  return buffer_size(buffer) == 0;
}

static inline void
buffer_split(Buffer *buffer)
{
  gsize buf_size = buffer_size(buffer);

  memmove(buffer->buffer, buffer_start(buffer), buf_size);
  buffer->size = buf_size;
  buffer->consumed = 0;
}

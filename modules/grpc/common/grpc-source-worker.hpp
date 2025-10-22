/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023-2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef GRPC_SOURCE_WORKER_HPP
#define GRPC_SOURCE_WORKER_HPP

#include "grpc-source.hpp"

typedef struct GrpcSourceWorker_ GrpcSourceWorker;

namespace syslogng {
namespace grpc {

class SourceWorker
{
public:
  SourceWorker(GrpcSourceWorker *s);
  virtual ~SourceWorker() {};

  virtual bool init() { return true; }
  virtual void deinit() {}
  virtual void run() = 0;
  virtual void request_exit() = 0;
  void post(LogMessage *msg);

public:
  GrpcSourceWorker *super;

protected:
  /* do not store the reference beyond local usage */
  SourceDriver &get_owner();
};

}
}

#include "compat/cpp-start.h"

GrpcSourceWorker *grpc_sw_new(GrpcSourceDriver *o, gint worker_index);

struct GrpcSourceWorker_
{
  LogThreadedSourceWorker super;
  syslogng::grpc::SourceWorker *cpp;
};

#include "compat/cpp-end.h"

#endif

/*
 * Copyright (c) 2023 Attila Szakacs
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

#ifndef SYSLOG_NG_OTLP_DEST_HPP
#define SYSLOG_NG_OTLP_DEST_HPP

#include "otel-dest.hpp"
#include "syslog-ng-otlp-dest.h"

namespace syslogng {
namespace grpc {
namespace otel {

class SyslogNgDestDriver : public DestDriver
{
public:
  using DestDriver::DestDriver;

  const char *format_stats_key(StatsClusterKeyBuilder *kb) override;
  const char *generate_persist_name() override;
  bool init() override;

  LogThreadedDestWorker *construct_worker(int worker_index) override;

private:
  const char *generate_legacy_persist_name();
  bool update_legacy_persist_name_if_exists();
};

}
}
}

#endif

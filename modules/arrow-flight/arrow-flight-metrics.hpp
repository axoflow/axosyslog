/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef ARROW_FLIGHT_METRICS_HPP
#define ARROW_FLIGHT_METRICS_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-key-builder.h"
#include "compat/cpp-end.h"

#include <map>
#include <string>

namespace syslog_ng {
namespace arrow_flight {

class DestDriverMetrics
{
public:
  void init(StatsClusterKeyBuilder *kb, int stats_level);
  void deinit();

private:
  StatsClusterKeyBuilder *kb = nullptr;
  std::map<std::string, StatsCluster *> request_clusters;
};

}
}

#endif

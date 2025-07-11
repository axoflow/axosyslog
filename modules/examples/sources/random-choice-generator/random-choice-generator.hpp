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

#ifndef RANDOM_CHOICE_GENERATOR_HPP
#define RANDOM_CHOICE_GENERATOR_HPP

#include "random-choice-generator.h"

#include "compat/cpp-start.h"
#include "logthrsource/logthrsourcedrv.h"
#include "compat/cpp-end.h"

#include <atomic>
#include <string>
#include <vector>

typedef struct RandomChoiceGeneratorSourceWorker_ RandomChoiceGeneratorSourceWorker;
typedef struct RandomChoiceGeneratorSourceDriver_ RandomChoiceGeneratorSourceDriver;

namespace syslogng {
namespace examples {
namespace random_choice_generator {

class SourceWorker;

class SourceDriver
{
public:
  SourceDriver(RandomChoiceGeneratorSourceDriver *s);

  void set_choices(GList *choices);
  void set_freq(gdouble freq);
  void format_stats_key(StatsClusterKeyBuilder *kb);
  gboolean init();
  gboolean deinit();
  void request_exit();

private:
  friend SourceWorker;

private:
  RandomChoiceGeneratorSourceDriver *super;
  std::atomic_bool exit_requested{false};
  std::vector<std::string> choices;
  gdouble freq = 1000;
};

class SourceWorker
{
public:
  SourceWorker(RandomChoiceGeneratorSourceWorker *s, SourceDriver &d);

  void run();
  void request_exit();

private:
  RandomChoiceGeneratorSourceWorker *super;
  SourceDriver &driver;
};

}
}
}

#include "compat/cpp-start.h"

struct RandomChoiceGeneratorSourceWorker_
{
  LogThreadedSourceWorker super;
  syslogng::examples::random_choice_generator::SourceWorker *cpp;
};

struct RandomChoiceGeneratorSourceDriver_
{
  LogThreadedSourceDriver super;
  syslogng::examples::random_choice_generator::SourceDriver *cpp;
};

#include "compat/cpp-end.h"

#endif

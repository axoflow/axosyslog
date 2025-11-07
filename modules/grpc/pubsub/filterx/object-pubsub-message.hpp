/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 shifter
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

#ifndef OBJECT_PUBSUB_MESSAGE_HPP
#define OBJECT_PUBSUB_MESSAGE_HPP

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "filterx/filterx-mapping.h"
#include "object-pubsub.h"
#include "compat/cpp-end.h"

#include "google/pubsub/v1/pubsub.grpc.pb.h"

typedef struct FilterXPubSubMessage_ FilterXPubSubMessage;

FilterXObject *_filterx_pubsub_message_clone(FilterXObject *s);

namespace syslogng {
namespace grpc {
namespace pubsub {
namespace filterx {

class Message
{
public:
  Message(FilterXPubSubMessage *super);
  Message(FilterXPubSubMessage *super, const std::string data, const std::map<std::string, std::string> &attributes);
  Message(FilterXPubSubMessage *super, FilterXObject *protobuf_object);
  Message(Message &o) = delete;
  Message(Message &&o) = delete;
  std::string marshal(void);
  const google::pubsub::v1::PubsubMessage &get_value() const;
  std::string repr() const;

  bool set_attribute(const std::string &key, const std::string &value);
  bool set_data(const std::string &data);
private:
  FilterXPubSubMessage *super;
  google::pubsub::v1::PubsubMessage message;
  Message(const Message &o, FilterXPubSubMessage *super);
  friend FilterXObject *::_filterx_pubsub_message_clone(FilterXObject *s);
};

}
}
}
}

#include "compat/cpp-start.h"

struct FilterXPubSubMessage_
{
  FilterXObject super;
  syslogng::grpc::pubsub::filterx::Message *cpp;
};

#include "compat/cpp-end.h"

#endif

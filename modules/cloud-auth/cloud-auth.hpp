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

#ifndef CLOUD_AUTH_HPP
#define CLOUD_AUTH_HPP

#include "cloud-auth.h"

namespace syslogng {
namespace cloud_auth {

class Authenticator
{
public:
  virtual ~Authenticator() {};
  virtual void handle_http_header_request(HttpHeaderRequestSignalData *data) = 0;

  virtual void handle_grpc_metadata_request(GrpcMetadataRequestSignalData *data)
  {
    data->result = GRPC_SLOT_PLUGIN_ERROR;
  }
};

}
}

#include "compat/cpp-start.h"

struct _CloudAuthenticator
{
  syslogng::cloud_auth::Authenticator *cpp;

  gboolean (*init)(CloudAuthenticator *s);
  void (*free_fn)(CloudAuthenticator *s);
};

#include "compat/cpp-end.h"

#endif

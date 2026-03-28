/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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

#ifndef AZURE_AUTH_HPP
#define AZURE_AUTH_HPP

#include "azure-auth.h"
#include "oauth2-auth.hpp"

namespace syslogng {
namespace cloud_auth {
namespace azure {

class AzureMonitorAuthenticator: public syslogng::cloud_auth::oauth2::OAuth2Authenticator
{
public:
  AzureMonitorAuthenticator(const char *tenant_id, const char *app_id,
                            const char *app_secret, const char *scope_);
  ~AzureMonitorAuthenticator() {};
};

}
}
}

#endif

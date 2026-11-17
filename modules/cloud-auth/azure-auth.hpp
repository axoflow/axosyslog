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
#include "cloud-auth.hpp"

#include <memory>
#include <mutex>
#include <jwt-cpp/jwt.h>

namespace syslogng {
namespace cloud_auth {
namespace azure {

class ClientCredential
{
public:
  virtual ~ClientCredential() {};
  virtual std::string form_fields(const std::string &client_id, const std::string &token_url) = 0;
};

class ClientSecret: public ClientCredential
{
public:
  ClientSecret(const char *secret);

  std::string form_fields(const std::string &client_id, const std::string &token_url);

private:
  std::string secret;
};

class ClientCertificate: public ClientCredential
{
public:
  ClientCertificate(const char *cert_path, const char *key_path);

  std::string form_fields(const std::string &client_id, const std::string &token_url);

private:
  std::string thumbprint;
  jwt::algorithm::ps256 signer;
};

class AzureMonitorAuthenticator: public syslogng::cloud_auth::Authenticator
{
public:
  AzureMonitorAuthenticator(const char *auth_url_base, const char *tenant_id, const char *app_id,
                            const char *scope, const char *ca_file, const char *ca_dir,
                            std::unique_ptr<ClientCredential> credential);
  ~AzureMonitorAuthenticator() {};

  void handle_http_header_request(HttpRequestSignalData *data);
  void handle_http_response(HttpResponseSignalData *data);

private:
  std::string auth_url;
  std::string app_id;
  std::string scope;
  std::string ca_file;
  std::string ca_dir;
  std::unique_ptr<ClientCredential> credential;

  std::mutex lock;
  std::string cached_token;
  std::chrono::system_clock::time_point refresh_token_after;

  std::string auth_body();
  void add_token_to_header(HttpRequestSignalData *data);
  bool parse_token_and_expiry_from_response(const std::string &response_payload,
                                            std::string &token, long *expiry);
  static size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp);
  bool send_token_post_request(std::string &response_payload_buffer);
};

}
}
}

#endif

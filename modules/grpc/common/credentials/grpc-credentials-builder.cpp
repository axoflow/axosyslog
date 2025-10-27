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

#include "grpc-credentials-builder.hpp"

#include "compat/cpp-start.h"
#include "messages.h"
#include "compat/cpp-end.h"

#include <fstream>
#include <sstream>

using namespace syslogng::grpc;

static bool
_get_file_content(const char *path, std::string &content)
{
  std::ifstream ifs(path);
  if (!ifs)
    {
      msg_error("gRPC: File not found", evt_tag_str("path", path));
      return false;
    }

  content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
  ifs.close();

  return true;
}

/* Server */

void
ServerCredentialsBuilder::set_mode(GrpcServerAuthMode mode_)
{
  mode = mode_;
}

bool
ServerCredentialsBuilder::validate() const
{
  switch (mode)
    {
    case GSAM_INSECURE:
      break;
    case GSAM_TLS:
      if (ssl_server_credentials_options.pem_key_cert_pairs.size() == 0 ||
          ssl_server_credentials_options.pem_key_cert_pairs.at(0).private_key.size() == 0 ||
          ssl_server_credentials_options.pem_key_cert_pairs.at(0).cert_chain.size() == 0)
        {
          msg_error("gRPC: You have a TLS enabled source without a X.509 keypair. "
                    "Make sure to set the auth(tls(key-file() and cert-file())) options");
          return false;
        }
      break;
    case GSAM_ALTS:
      break;
    default:
      g_assert_not_reached();
    }

  return build().get() != nullptr;
}

std::shared_ptr<::grpc::ServerCredentials>
ServerCredentialsBuilder::build() const
{
  switch (mode)
    {
    case GSAM_INSECURE:
      return ::grpc::InsecureServerCredentials();
    case GSAM_TLS:
    {
      return ::grpc::SslServerCredentials(ssl_server_credentials_options);
    }
    case GSAM_ALTS:
      return ::grpc::experimental::AltsServerCredentials(alts_server_credentials_options);
    default:
      g_assert_not_reached();
    }
  g_assert_not_reached();
}

bool
ServerCredentialsBuilder::set_tls_ca_path(const char *ca_path_)
{
  this->ca_path = ca_path_;
  return _get_file_content(ca_path_, ssl_server_credentials_options.pem_root_certs);
}

bool
ServerCredentialsBuilder::set_tls_key_path(const char *key_path)
{
  if (ssl_server_credentials_options.pem_key_cert_pairs.size() == 0)
    {
      ::grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair;
      ssl_server_credentials_options.pem_key_cert_pairs.push_back(key_cert_pair);
    }

  return _get_file_content(key_path, ssl_server_credentials_options.pem_key_cert_pairs.at(0).private_key);
}

bool
ServerCredentialsBuilder::set_tls_cert_path(const char *cert_path_)
{
  if (ssl_server_credentials_options.pem_key_cert_pairs.size() == 0)
    {
      ::grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair;
      ssl_server_credentials_options.pem_key_cert_pairs.push_back(key_cert_pair);
    }

  this->cert_path = cert_path_;

  return _get_file_content(cert_path_, ssl_server_credentials_options.pem_key_cert_pairs.at(0).cert_chain);
}

void
ServerCredentialsBuilder::set_tls_peer_verify(GrpcServerTlsPeerVerify peer_verify)
{
  grpc_ssl_client_certificate_request_type client_certificate_request;

  switch (peer_verify)
    {
    case GSTPV_OPTIONAL_UNTRUSTED:
      client_certificate_request = GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
      break;
    case GSTPV_OPTIONAL_TRUSTED:
      client_certificate_request = GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY;
      break;
    case GSTPV_REQUIRED_UNTRUSTED:
      client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY;
      break;
    case GSTPV_REQUIRED_TRUSTED:
      client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
      break;
    default:
      g_assert_not_reached();
    }

  ssl_server_credentials_options.client_certificate_request = client_certificate_request;
}

std::string
ServerCredentialsBuilder::get_unique_id()
{
  std::ostringstream id;

  std::string peer_verify;
  switch (this->ssl_server_credentials_options.client_certificate_request)
    {
    case GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE:
      peer_verify = "dont_request_client_cert";
      break;
    case GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY:
      peer_verify = "request_client_cert_and_verify";
      break;
    case GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
      peer_verify = "require_client_cert_but_dont_verify";
      break;
    case GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY:
      peer_verify = "require_client_cert_and_verify";
      break;
    default:
      g_assert_not_reached();
    }

  switch (this->mode)
    {
    case GSAM_INSECURE:
      id << "insecure";
      break;
    case GSAM_TLS:
      id << "tls(" << peer_verify << ", " << this->ca_path << ", " << this->cert_path << ")";
      break;
    case GSAM_ALTS:
      id << "alts";
      break;
    default:
      g_assert_not_reached();
    }

  return id.str();
}

void
grpc_server_credentials_builder_set_mode(GrpcServerCredentialsBuilderW *s, GrpcServerAuthMode mode)
{
  s->self->set_mode(mode);
}

gboolean
grpc_server_credentials_builder_set_tls_ca_path(GrpcServerCredentialsBuilderW *s, const gchar *ca_path)
{
  return s->self->set_tls_ca_path(ca_path);
}

gboolean
grpc_server_credentials_builder_set_tls_key_path(GrpcServerCredentialsBuilderW *s, const gchar *key_path)
{
  return s->self->set_tls_key_path(key_path);
}

gboolean
grpc_server_credentials_builder_set_tls_cert_path(GrpcServerCredentialsBuilderW *s, const gchar *cert_path)
{
  return s->self->set_tls_cert_path(cert_path);
}

void
grpc_server_credentials_builder_set_tls_peer_verify(GrpcServerCredentialsBuilderW *s,
                                                    GrpcServerTlsPeerVerify peer_verify)
{
  s->self->set_tls_peer_verify(peer_verify);
}

/* Client */


void
ClientCredentialsBuilder::set_mode(GrpcClientAuthMode mode_)
{
  mode = mode_;
}

bool
ClientCredentialsBuilder::set_tls_ca_path(const char *ca_path)
{
  return _get_file_content(ca_path, ssl_credentials_options.pem_root_certs);
}

bool
ClientCredentialsBuilder::set_tls_key_path(const char *key_path)
{
  return _get_file_content(key_path, ssl_credentials_options.pem_private_key);
}

bool
ClientCredentialsBuilder::set_tls_cert_path(const char *cert_path)
{
  return _get_file_content(cert_path, ssl_credentials_options.pem_cert_chain);
}

void
ClientCredentialsBuilder::add_alts_target_service_account(const char *target_service_account)
{
  alts_credentials_options.target_service_accounts.push_back(target_service_account);
}

bool
ClientCredentialsBuilder::set_service_account_key_path(const char *key_path)
{
  return _get_file_content(key_path, service_account.key);
}

void
ClientCredentialsBuilder::set_service_account_validity_duration(guint64 validity_duration)
{
  service_account.validity_duration = validity_duration;
}

void
ClientCredentialsBuilder::set_adc_service_account_key(const char *key_path)
{
  adc_service_account_key = key_path;
}

bool
ClientCredentialsBuilder::validate() const
{
  switch (mode)
    {
    case GCAM_INSECURE:
      break;
    case GCAM_TLS:
      break;
    case GCAM_ALTS:
      break;
    case GCAM_ADC:
      break;
    case GCAM_SERVICE_ACCOUNT:
      if (service_account.key.empty())
        {
          msg_error("gRPC: Service account configuration requires the path to the "
                    "json file containing the service account key");
          return false;
        }
      break;
    default:
      g_assert_not_reached();
    }

  return build().get() != nullptr;
}

std::shared_ptr<::grpc::ChannelCredentials>
ClientCredentialsBuilder::build() const
{
  switch (mode)
    {
    case GCAM_INSECURE:
      return ::grpc::InsecureChannelCredentials();
    case GCAM_TLS:
      return ::grpc::SslCredentials(ssl_credentials_options);
    case GCAM_ALTS:
      return ::grpc::experimental::AltsCredentials(alts_credentials_options);
    case GCAM_ADC:
    {
      if (!adc_service_account_key.empty())
        {
          const char *env_var_name = "GOOGLE_APPLICATION_CREDENTIALS";
          const char *key_path_orig = getenv(env_var_name);
          if (setenv(env_var_name, adc_service_account_key.c_str(), 1) != 0)
            {
              msg_error("gRPC: setenv failed for GOOGLE_APPLICATION_CREDENTIALS");
              return nullptr;
            }
          std::shared_ptr<::grpc::ChannelCredentials> creds = ::grpc::GoogleDefaultCredentials();
          if (!key_path_orig)
            {
              unsetenv(env_var_name);
              return creds;
            }
          if (setenv(env_var_name, key_path_orig, 1) != 0)
            {
              msg_error("gRPC: resetting GOOGLE_APPLICATION_CREDENTIALS failed");
              return nullptr;
            }
          return creds;
        }
      return ::grpc::GoogleDefaultCredentials();
    }
    case GCAM_SERVICE_ACCOUNT:
    {
      auto channel_creds = ::grpc::SslCredentials(::grpc::SslCredentialsOptions());
      auto call_creds = ::grpc::ServiceAccountJWTAccessCredentials(service_account.key,
                        service_account.validity_duration);
      if (!call_creds)
        {
          msg_error("gRPC: The specified file doesn't contain a service account key");
          return nullptr;
        }
      return ::grpc::CompositeChannelCredentials(channel_creds, call_creds);
    }
    default:
      g_assert_not_reached();
    }
  g_assert_not_reached();
}

void
grpc_client_credentials_builder_set_mode(GrpcClientCredentialsBuilderW *s, GrpcClientAuthMode mode)
{
  s->self->set_mode(mode);
}

gboolean
grpc_client_credentials_builder_set_tls_ca_path(GrpcClientCredentialsBuilderW *s, const gchar *ca_path)
{
  return s->self->set_tls_ca_path(ca_path);
}

gboolean
grpc_client_credentials_builder_set_tls_key_path(GrpcClientCredentialsBuilderW *s, const gchar *key_path)
{
  return s->self->set_tls_key_path(key_path);
}

gboolean
grpc_client_credentials_builder_set_tls_cert_path(GrpcClientCredentialsBuilderW *s, const gchar *cert_path)
{
  return s->self->set_tls_cert_path(cert_path);
}

void
grpc_client_credentials_builder_add_alts_target_service_account(GrpcClientCredentialsBuilderW *s,
    const gchar *target_service_acount)
{
  return s->self->add_alts_target_service_account(target_service_acount);
}

gboolean
grpc_client_credentials_builder_service_account_set_key(GrpcClientCredentialsBuilderW *s, const gchar *key_path)
{
  return s->self->set_service_account_key_path(key_path);
}

void
grpc_client_credentials_builder_service_account_set_validity_duration(GrpcClientCredentialsBuilderW *s,
    guint64 validity_duration)
{
  s->self->set_service_account_validity_duration(validity_duration);
}

void
grpc_client_credentials_builder_set_adc_service_account_key(GrpcClientCredentialsBuilderW *s, const gchar *key_path)
{
  return s->self->set_adc_service_account_key(key_path);
}

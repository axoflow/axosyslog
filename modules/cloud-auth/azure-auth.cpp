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
#include "azure-auth.hpp"

#include "compat/cpp-start.h"
#include "scratch-buffers.h"
#include "uuid.h"
#include "compat/cpp-end.h"

#include <fstream>
#include <sstream>
#include <openssl/pem.h>
#include <openssl/x509.h>

using namespace syslogng::cloud_auth::azure;

ClientSecret::ClientSecret(const char *secret_)
  : secret(secret_)
{
}

std::string
ClientSecret::form_fields(const std::string &, const std::string &)
{
  return "client_secret=" + secret;
}

static std::string
_read_file(const char *path)
{
  std::ifstream file(path);
  if (!file)
    throw std::runtime_error(std::string("Failed to open ") + path);

  std::stringstream content;
  content << file.rdbuf();
  return content.str();
}

static std::string
_certificate_thumbprint(const std::string &cert_pem)
{
  std::unique_ptr<BIO, decltype(&BIO_free_all)> bio(BIO_new_mem_buf(cert_pem.data(), cert_pem.size()), BIO_free_all);
  std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
  if (!cert)
    throw std::runtime_error("Failed to parse the certificate");

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len;
  if (!X509_digest(cert.get(), EVP_sha256(), digest, &digest_len))
    throw std::runtime_error("Failed to compute the certificate thumbprint");

  std::string encoded = jwt::base::encode<jwt::alphabet::base64url>(std::string((const char *) digest, digest_len));
  return jwt::base::trim<jwt::alphabet::base64url>(encoded);
}

static std::string
_uuid()
{
  gchar buf[37];
  uuid_gen_random(buf, sizeof(buf));
  return buf;
}

ClientCertificate::ClientCertificate(const char *cert_path, const char *key_path)
  : thumbprint(_certificate_thumbprint(_read_file(cert_path))), signer("", _read_file(key_path), "", "")
{
}

std::string
ClientCertificate::form_fields(const std::string &client_id, const std::string &token_url)
{
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  std::string assertion = jwt::create()
                          .set_type("JWT")
                          .set_header_claim("x5t#S256", jwt::claim(thumbprint))
                          .set_issuer(client_id)
                          .set_subject(client_id)
                          .set_audience(token_url)
                          .set_id(_uuid())
                          .set_not_before(now)
                          .set_expires_at(now + std::chrono::minutes{5})
                          .sign(signer);

  return "client_assertion_type=urn:ietf:params:oauth:client-assertion-type:jwt-bearer&client_assertion=" + assertion;
}

AzureMonitorAuthenticator::AzureMonitorAuthenticator(const char *auth_url_base,
                                                     const char *tenant_id,
                                                     const char *app_id_,
                                                     const char *scope_,
                                                     const char *ca_file_,
                                                     const char *ca_dir_,
                                                     std::unique_ptr<ClientCredential> credential_)
  : app_id(app_id_), scope(scope_), ca_file(ca_file_ ? ca_file_ : ""), ca_dir(ca_dir_ ? ca_dir_ : ""),
    credential(std::move(credential_))
{
  auth_url = auth_url_base;
  if (!auth_url.empty() && auth_url.back() != '/')
    auth_url.append("/");
  auth_url.append(tenant_id);
  auth_url.append("/oauth2/v2.0/token");
}

std::string
AzureMonitorAuthenticator::auth_body()
{
  return "grant_type=client_credentials&client_id=" + app_id + "&scope=" + scope + "&"
         + credential->form_fields(app_id, auth_url);
}

void AzureMonitorAuthenticator::handle_http_header_request(HttpRequestSignalData *data)
{
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

  lock.lock();

  if (now <= refresh_token_after && !cached_token.empty())
    {
      add_token_to_header(data);
      lock.unlock();

      data->result = HTTP_SLOT_SUCCESS;
      return;
    }

  cached_token.clear();

  std::string response_payload_buffer;
  if (!send_token_post_request(response_payload_buffer))
    {
      lock.unlock();

      data->result = HTTP_SLOT_CRITICAL_ERROR;
      return;
    }

  long expiry;
  if (!parse_token_and_expiry_from_response(response_payload_buffer, cached_token, &expiry))
    {
      lock.unlock();

      data->result = HTTP_SLOT_CRITICAL_ERROR;
      return;
    }

  refresh_token_after = now + std::chrono::seconds{expiry - 60};
  add_token_to_header(data);

  lock.unlock();

  data->result = HTTP_SLOT_SUCCESS;
}

void
AzureMonitorAuthenticator::handle_http_response(HttpResponseSignalData *data)
{
  if (data->http_code != 401)
    return;

  lock.lock();
  cached_token.clear();
  lock.unlock();

  data->result = HTTP_SLOT_RESOLVED;
}

void
AzureMonitorAuthenticator::add_token_to_header(HttpRequestSignalData *data)
{
  /* Scratch Buffers are marked at this point in http-worker.c */
  GString *auth_buffer = scratch_buffers_alloc();
  g_string_append(auth_buffer, "Authorization: Bearer ");
  g_string_append(auth_buffer, cached_token.c_str());

  list_append(data->request_headers, auth_buffer->str);
}

bool
AzureMonitorAuthenticator::send_token_post_request(std::string &response_payload_buffer)
{
  CURLcode ret;
  std::string body;
  CURL *hnd = NULL;

  try
    {
      body = auth_body();
    }
  catch (const std::exception &e)
    {
      msg_error("cloud_auth::azure::AzureMonitorAuthenticator: "
                "failed to build the token request",
                evt_tag_str("url", auth_url.c_str()),
                evt_tag_str("error", e.what()));
      return false;
    }

  hnd = curl_easy_init();
  if (!hnd)
    {
      msg_error("cloud_auth::azure::AzureMonitorAuthenticator: "
                "failed to init cURL handle",
                evt_tag_str("url", auth_url.c_str()));
      goto error;
    }

  curl_easy_setopt(hnd, CURLOPT_URL, auth_url.c_str());
  curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "POST");
  curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(hnd, CURLOPT_WRITEDATA, (void *) &response_payload_buffer);
  if (!ca_file.empty())
    curl_easy_setopt(hnd, CURLOPT_CAINFO, ca_file.c_str());
  if (!ca_dir.empty())
    curl_easy_setopt(hnd, CURLOPT_CAPATH, ca_dir.c_str());

  ret = curl_easy_perform(hnd);
  if (ret != CURLE_OK)
    {
      msg_error("cloud_auth::azure::AzureMonitorAuthenticator: "
                "error sending HTTP request to metadata server",
                evt_tag_str("url", auth_url.c_str()),
                evt_tag_str("error", curl_easy_strerror(ret)));
      goto error;
    }

  long http_result_code;
  ret = curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &http_result_code);
  if (ret != CURLE_OK)
    {
      msg_error("cloud_auth::azure::AzureMonitorAuthenticator: "
                "failed to get HTTP result code",
                evt_tag_str("url", auth_url.c_str()),
                evt_tag_str("error", curl_easy_strerror(ret)));
      goto error;
    }

  if (http_result_code != 200)
    {
      msg_error("cloud_auth::azure::AzureMonitorAuthenticator: "
                "non 200 HTTP result code received",
                evt_tag_str("url", auth_url.c_str()),
                evt_tag_int("http_result_code", http_result_code));
      goto error;
    }

  curl_easy_cleanup(hnd);
  return true;

error:
  if (hnd)
    {
      curl_easy_cleanup(hnd);
    }
  return false;
}

bool
AzureMonitorAuthenticator::parse_token_and_expiry_from_response(const std::string &response_payload,
    std::string &token, long *expiry)
{
  picojson::value json;
  std::string json_parse_error = picojson::parse(json, response_payload);
  if (!json_parse_error.empty())
    {
      msg_error("cloud_auth::azure::AzureMonitorAuthenticator: "
                "failed to parse response JSON",
                evt_tag_str("url", auth_url.c_str()),
                evt_tag_str("response_json", response_payload.c_str()));
      return false;
    }

  if (!json.is<picojson::object>() || !json.contains("access_token") || !json.contains("expires_in")
      || !json.get("access_token").is<std::string>() || !json.get("expires_in").is<double>())
    {
      msg_error("cloud_auth::azure::AzureMonitorAuthenticator: "
                "unexpected response JSON",
                evt_tag_str("url", auth_url.c_str()),
                evt_tag_str("response_json", response_payload.c_str()));
      return false;
    }

  token.assign(json.get("access_token").get<std::string>());
  *expiry = lround(json.get("expires_in").get<double>()); /* getting a long from picojson is not always available */
  return true;
}

size_t
AzureMonitorAuthenticator::curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
  const char *data = (const char *) contents;
  std::string *response_payload_buffer = (std::string *) userp;

  size_t real_size = size * nmemb;
  response_payload_buffer->append(data, real_size);

  return real_size;
}

/* C Wrappers */

typedef struct AzureAuthenticator
{
  CloudAuthenticator super;

  AzureAuthenticatorAuthMode auth_mode;

  gchar *tenant_id;
  gchar *app_id;
  gchar *scope;
  gchar *app_secret;
  gchar *cert_file;
  gchar *key_file;
  gchar *auth_url;
  gchar *ca_file;
  gchar *ca_dir;
} _AzureAuthenticator;

void
azure_authenticator_set_auth_mode(CloudAuthenticator *s, AzureAuthenticatorAuthMode auth_mode)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  self->auth_mode = auth_mode;
}

void
azure_authenticator_set_tenant_id(CloudAuthenticator *s, const gchar *tenant_id)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->tenant_id);
  self->tenant_id = g_strdup(tenant_id);
}

void
azure_authenticator_set_app_id(CloudAuthenticator *s, const gchar *app_id)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->app_id);
  self->app_id = g_strdup(app_id);
}

void
azure_authenticator_set_scope(CloudAuthenticator *s, const gchar *scope)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->scope);
  self->scope = g_strdup(scope);
}

void
azure_authenticator_set_app_secret(CloudAuthenticator *s, const gchar *app_secret)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->app_secret);
  self->app_secret = g_strdup(app_secret);
}

void
azure_authenticator_set_cert_file(CloudAuthenticator *s, const gchar *cert_file)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->cert_file);
  self->cert_file = g_strdup(cert_file);
}

void
azure_authenticator_set_key_file(CloudAuthenticator *s, const gchar *key_file)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->key_file);
  self->key_file = g_strdup(key_file);
}

void
azure_authenticator_set_auth_url(CloudAuthenticator *s, const gchar *auth_url)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->auth_url);
  self->auth_url = g_strdup(auth_url);
}

void
azure_authenticator_set_ca_file(CloudAuthenticator *s, const gchar *ca_file)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->ca_file);
  self->ca_file = g_strdup(ca_file);
}

void
azure_authenticator_set_ca_dir(CloudAuthenticator *s, const gchar *ca_dir)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->ca_dir);
  self->ca_dir = g_strdup(ca_dir);
}

static std::unique_ptr<ClientCredential>
_create_credential(AzureAuthenticator *self)
{
  if (self->cert_file || self->key_file)
    {
      if (!self->cert_file || !self->key_file)
        throw std::runtime_error("cert_file() and key_file() must be set together");
      if (self->app_secret && self->app_secret[0])
        throw std::runtime_error("app_secret() cannot be set together with cert_file() and key_file()");
      return std::unique_ptr<ClientCredential>(new ClientCertificate(self->cert_file, self->key_file));
    }

  if (!self->app_secret)
    throw std::runtime_error("app_secret() or cert_file() with key_file() is mandatory");
  return std::unique_ptr<ClientCredential>(new ClientSecret(self->app_secret));
}

static gboolean
_init(CloudAuthenticator *s)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  switch (self->auth_mode)
    {
    case AAAM_MONITOR:
      try
        {
          self->super.cpp = new AzureMonitorAuthenticator(self->auth_url,
                                                          self->tenant_id,
                                                          self->app_id,
                                                          self->scope,
                                                          self->ca_file,
                                                          self->ca_dir,
                                                          _create_credential(self));
        }
      catch (const std::runtime_error &e)
        {
          msg_error("cloud_auth::azure: Failed to initialize AzureMonitorAuthenticator",
                    evt_tag_str("error", e.what()));
          return FALSE;
        }
      break;
    case AAAM_UNDEFINED:
      msg_error("cloud_auth::azure: Failed to initialize AzureMonitorAuthenticator",
                evt_tag_str("error", "Authentication mode must be set (e.g. monitor())"));
      return FALSE;
    default:
      g_assert_not_reached();
    }

  return TRUE;
}

static void
_free(CloudAuthenticator *s)
{
  AzureAuthenticator *self = (AzureAuthenticator *) s;

  g_free(self->tenant_id);
  g_free(self->app_id);
  g_free(self->scope);
  g_free(self->app_secret);
  g_free(self->cert_file);
  g_free(self->key_file);
  g_free(self->auth_url);
  g_free(self->ca_file);
  g_free(self->ca_dir);
}

static void
_set_default_options(AzureAuthenticator *self)
{
  self->scope = g_strdup("https://monitor.azure.com//.default");
  self->auth_url = g_strdup("https://login.microsoftonline.com");
}

CloudAuthenticator *
azure_authenticator_new(void)
{
  AzureAuthenticator *self = g_new0(AzureAuthenticator, 1);

  self->super.init = _init;
  self->super.free_fn = _free;

  _set_default_options(self);

  return &self->super;
}

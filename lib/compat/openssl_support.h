/*
 * Copyright (c) 2002-2016 Balabit
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

#ifndef OPENSSL_SUPPORT_H_INCLUDED
#define OPENSSL_SUPPORT_H_INCLUDED

#include "compat/compat.h"
#include <openssl/ssl.h>
#include <openssl/dh.h>
#include <glib.h>

#if !SYSLOG_NG_HAVE_DECL_SSL_CTX_GET0_PARAM
X509_VERIFY_PARAM *SSL_CTX_get0_param(SSL_CTX *ctx);
#endif

#if !SYSLOG_NG_HAVE_DECL_X509_STORE_CTX_GET0_CERT
X509 *X509_STORE_CTX_get0_cert(X509_STORE_CTX *ctx);
#endif

#if !SYSLOG_NG_HAVE_DECL_X509_GET_EXTENSION_FLAGS
#include <stdint.h>
uint32_t X509_get_extension_flags(X509 *x);
#endif

#if SYSLOG_NG_HAVE_DECL_EVP_MD_CTX_RESET
#include <openssl/evp.h>
#define EVP_MD_CTX_cleanup EVP_MD_CTX_reset
#define DECLARE_EVP_MD_CTX(md_ctx) EVP_MD_CTX * md_ctx = EVP_MD_CTX_create()
#else
#define DECLARE_EVP_MD_CTX(md_ctx) EVP_MD_CTX _##md_ctx; EVP_MD_CTX * md_ctx = & _##md_ctx
#define EVP_MD_CTX_destroy(md_ctx) EVP_MD_CTX_cleanup(md_ctx)
#endif

#if !SYSLOG_NG_HAVE_DECL_ASN1_STRING_GET0_DATA
#define ASN1_STRING_get0_data ASN1_STRING_data
#endif

#if OPENSSL_VERSION_NUMBER < 0x30000000L
#define SYSLOG_NG_HAVE_DECL_DIGEST_MD4 1
#endif

#if !SYSLOG_NG_HAVE_DECL_DH_SET0_PQG && OPENSSL_VERSION_NUMBER < 0x30000000L
int DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g);
#endif

#if !SYSLOG_NG_HAVE_DECL_BN_GET_RFC3526_PRIME_2048
BIGNUM *BN_get_rfc3526_prime_2048(BIGNUM *bn);
#endif

void openssl_ctx_setup_session_tickets(SSL_CTX *ctx);

void openssl_ctx_setup_ecdh(SSL_CTX *ctx);
gboolean openssl_ctx_setup_dh(SSL_CTX *ctx);
gboolean openssl_ctx_load_dh_from_file(SSL_CTX *ctx, const gchar *dhparam_file);

void openssl_init(void);
void openssl_crypto_init_threading(void);
void openssl_crypto_deinit_threading(void);

#endif

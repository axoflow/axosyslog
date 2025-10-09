/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2011 Balázs Scheidler
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
 */
#ifndef TRANSPORT_TLS_SESSION_H_INCLUDED
#define TRANSPORT_TLS_SESSION_H_INCLUDED

#include "tls-verifier.h"

#define X509_MAX_CN_LEN 64
#define X509_MAX_O_LEN 64
#define X509_MAX_OU_LEN 32
#define X509_MAX_FP_LEN 256

typedef struct _TLSContext TLSContext;
typedef struct _TLSSession
{
  SSL *ssl;
  TLSContext *ctx;
  TLSVerifier *verifier;
  struct
  {
    int found;
    gchar o[X509_MAX_O_LEN];
    gchar ou[X509_MAX_OU_LEN];
    gchar cn[X509_MAX_CN_LEN];
    gchar fingerprint[X509_MAX_FP_LEN];
  } peer_info;
} TLSSession;

void tls_session_configure_allow_compress(TLSSession *tls_session, gboolean allow_compress);
void tls_session_set_verifier(TLSSession *self, TLSVerifier *verifier);

int tls_session_verify_callback(int ok, X509_STORE_CTX *ctx);
int tls_session_ocsp_client_verify_callback(SSL *ssl, void *user_data);

TLSSession *tls_session_new(SSL *ssl, TLSContext *ctx);
void tls_session_free(TLSSession *self);

#endif

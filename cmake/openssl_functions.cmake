#############################################################################
# Copyright (c) 2018 Balabit
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################

function (openssl_set_defines)
  set (CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR})
  set (CMAKE_REQUIRED_LIBRARIES ${OPENSSL_LIBRARIES})
  include(CheckSymbolExists)
  set (check_symbol_headers
    openssl/asn1.h
    openssl/evp.h
    openssl/dh.h
    openssl/ssl.h
    openssl/x509v3.h
    openssl/bn.h)

  set (symbol_list
    EVP_MD_CTX_reset
    ASN1_STRING_get0_data
    SSL_CTX_get0_param
    X509_STORE_CTX_get0_cert
    X509_get_extension_flags
    DH_set0_pqg
    BN_get_rfc3526_prime_2048
    SSL_CONF_CTX_new
    SSL_CTX_set_num_tickets
    SSL_CTX_set_ciphersuites
    SSL_CTX_set1_sigalgs_list
    SSL_CTX_set1_client_sigalgs_list
    SSL_CTX_set_min_proto_version
    SSL_add_file_cert_subjects_to_stack
    SSL_add_dir_cert_subjects_to_stack)

  foreach (symbol ${symbol_list})
    string(TOUPPER ${symbol} SYMBOL_UPPERCASE)
    check_symbol_exists(${symbol} "${check_symbol_headers}" SYSLOG_NG_HAVE_DECL_${SYMBOL_UPPERCASE})
    set(SYSLOG_NG_HAVE_DECL_${SYMBOL_UPPERCASE} "${SYSLOG_NG_HAVE_DECL_${SYMBOL_UPPERCASE}}" PARENT_SCOPE)
  endforeach()
endfunction()

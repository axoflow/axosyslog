`opentelemetry()` source: set the `.tls.x509_cn`, `.tls.x509_o` and
`.tls.x509_ou` name-value pairs from the client certificate, the same way the
`network()` and `syslog()` sources do it.

The values are set when the client sends a certificate, which requires
`auth(tls(peer-verify()))` to be set to something other than
`optional-untrusted`.

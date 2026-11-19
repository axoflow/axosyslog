`transport(auto)`: TLS connections are now auto-detected

A `syslog()` or `network()` source with `transport(auto)` recognizes a TLS handshake and switches to TLS when `tls()`
is configured. Plaintext connections are rejected unless `tls(optional(yes))` is set, which accepts both plaintext and
TLS clients on the same port. TLS alerts and binary data that match no supported protocol are rejected with an error.

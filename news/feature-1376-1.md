`tls()`: Added the opt-in `ssl-options(partial-chain)` value, for sources and destinations.

With it, a certificate in `ca-file()` or `ca-dir()` is a trust anchor even if it is not a self-signed root, for
example an intermediate CA. Verification then stops at that certificate, so the validity, CRL and name constraints of
the certificates above it are not checked.

```
destination { network("logs.example" transport("tls") tls(ca-file("intermediate.crt") ssl-options(partial-chain))); };
```

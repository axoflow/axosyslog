`http()` source: added TLS support

The `http()` source can now terminate TLS using the `tls()` option block, with
the same options as the other server drivers (`key-file()`, `cert-file()`,
`ca-file()`, `peer-verify()`, `cipher-suite()`, etc.).  Each accepted
connection now runs over the `LogTransport` abstraction, so plain and encrypted
connections share the same I/O path.

```
source s_http {
    http(
        port(8443)
        path("/ingest")
        tls(
            key-file("/path/to/server.key")
            cert-file("/path/to/server.crt")
            peer-verify(optional-untrusted)
        )
    );
};
```

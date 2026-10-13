`--list-paths`: fixed an error on a missing `in-list()` file, and on a missing `key-file()`, `cert-file()` or `ca-file()`
in the `tls()` of `opentelemetry()` and the other gRPC based drivers. These files are read while the config is parsed,
so `--list-paths` failed instead of listing them.

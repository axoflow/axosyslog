`opentelemetry()` source: Added `mode()` option.

* `mode(logmessage)`: The old behavior, creating `${.otel_raw.<...>}` NVs.
* `mode(filterx-dict)`: Creates declared `resource`, `scope` and `log`
  FilterX variables, each holding a FilterX dict.

`mode(filterx-dict)` skips the serialization and deserialization
of the `${.otel_raw.<...>}` NVs, which improves performance.

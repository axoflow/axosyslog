`opentelemetry()` source: Added `mode()` option.

* `mode(logmessage)`: The old behavior, creating `${.otel_raw.<...>}` NVs.
* `mode(filterx)`: Creates declared `resource`, `scope` and `log` FilterX variables.

It is advised to set `mode(filterx)`, as it has significant performance improvement
by skipping unnecessary serializations and deserializations.

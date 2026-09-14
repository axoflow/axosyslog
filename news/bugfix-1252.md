`filterx`: Fixed a crash when `isset()` was called on a repeated field of an OpenTelemetry object, for example
`isset(log.attributes)` on an `otel_logrecord()`. These fields are always defined by the protocol, so `isset()`
returns true for them.

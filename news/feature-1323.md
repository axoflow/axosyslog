`network()`, `syslog()`, `tcp()`, `unix-*()`, `http()` destinations: Added the `syslogng_output_connection_failures_total` metric.

The counter has a `reason` label with the class of each lost or failed connection. Socket destinations report
`refused`, `timeout`, `no-route`, `reset`, `eof`, `resolve` or `error` from the errno of the failing call, the `http()`
destination reports `resolve`, `connect`, `timeout`, `reset`, `tls` or `error` from the curl error class.

Until now `syslogng_output_unreachable` flipped with no reason, and an `http()` request that failed before a response
arrived was not counted at all.

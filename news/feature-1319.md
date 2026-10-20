`arrow-flight`: Added the `syslogng_output_grpc_requests_total` metric, counting every batch write by its
`response_code`, like the gRPC-based destinations do.

The response codes follow Arrow Flight's status codes (`ok`, `unavailable`, `unauthenticated`, ...).

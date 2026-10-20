`arrow-flight()`: Added `response-action()` option

With this option, it is possible to fine tune how AxoSyslog
behaves in case of different Arrow Flight results.

The keys are Arrow status codes, not gRPC ones: the Arrow Flight
client maps the server's gRPC status onto an Arrow status, and
several gRPC codes land on the same Arrow code.

Supported Arrow results and their default action:
  * `io-error` (gRPC `UNAVAILABLE`, `INTERNAL`, `ABORTED`, `DATA_LOSS`,
    `DEADLINE_EXCEEDED`, `PERMISSION_DENIED`, `UNAUTHENTICATED`): disconnect
  * `cancelled` (gRPC `CANCELLED`): disconnect
  * `unknown-error` (gRPC `UNKNOWN` and any unlisted code): disconnect
  * `invalid` (gRPC `INVALID_ARGUMENT`, `RESOURCE_EXHAUSTED`,
    `FAILED_PRECONDITION`, `OUT_OF_RANGE`): drop
  * `key-error` (gRPC `NOT_FOUND`): drop
  * `already-exists` (gRPC `ALREADY_EXISTS`): drop
  * `not-implemented` (gRPC `UNIMPLEMENTED`): drop

Supported actions:
  * `disconnect`: reconnects and retries indefinitely, after `time-reopen()`
  * `retry`: retries the batch up to `retries()` times, then drops it
  * `drop`
  * `success`

Usage:
```
arrow-flight(
  url("grpc://localhost:8815")
  path("logs")
  schema("message" STRING => $MSG)
  response-action(
    invalid => disconnect
    io-error => drop
  )
);
```

`bigquery()`: fix crash when the server restarts the AppendRows stream

The BigQuery destination opens a bidirectional `AppendRows` streaming call per
worker and half-closes it with `WritesDone()` + `Finish()` on disconnect. When
the BigQuery Storage Write API restarts the stream (the server may do this at
any time and asks the client to reconnect), the in-flight `Write()`/`Read()`
fails and the gRPC call reaches a terminal state. `disconnect()` then issued the
half-close handshake on that already-completed call, which made gRPC core abort
the process with `GRPC_CALL_ERROR_TOO_MANY_OPERATIONS` (SIGSEGV, exit code 139),
crash-looping the destination.

The worker now tracks streaming-call failure and skips the half-close handshake
on a writer that has already failed, reconnecting cleanly instead. The
independent `FinalizeWriteStream()` unary RPC is unaffected.

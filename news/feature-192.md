`opentelemetry()`, `loki()`, `bigquery()` destination: Added `headers()` option

With this option you can add gRPC headers to each RPC call.

Example config:
```
opentelemetry(
  ...
  headers(
    "organization" => "Axoflow"
    "stream-name" => "axo-stream"
  )
);
```

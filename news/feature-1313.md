`opentelemetry()`, `axosyslog-otlp()`, `clickhouse()`, `loki()`, `bigquery()`, `google-pubsub-grpc()`, `arrow-flight()`
destinations: Added the `idle-reconnect-timeout()` option.

When a worker's connection has carried no request for longer than the given number of seconds, the worker opens a
fresh connection before it sends the next batch, instead of writing into a flow that a load balancer, NAT or
firewall may have silently forgotten. The default is 0, which disables it.
